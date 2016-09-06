/* Copyright (c) 2014-2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "shortcuts.h"
#include "win32-shortcuts.h"

#include <QString>

void Shortcuts::free_binding(K& key)
{
#ifndef _WIN32
    if (key)
    {
        key->setEnabled(false);
        key->setShortcut(QKeySequence::UnknownKey);
        std::shared_ptr<QxtGlobalShortcut> tmp(nullptr);
        key.swap(tmp);
    }
#else
    key.keycode = 0;
    key.guid = "";
#endif
}

void Shortcuts::bind_shortcut(K &key, const key_opts& k, unused_on_unix(bool, held))
{
#if !defined(_WIN32)
    using sh = QxtGlobalShortcut;
    if (key)
    {
        free_binding(key);
    }

    key = std::make_shared<sh>();

    if (k.keycode != "")
    {
        key->setShortcut(QKeySequence::fromString(k.keycode, QKeySequence::PortableText));
        key->setEnabled();
    }
}
#else
    key = K();
    int idx = 0;
    QKeySequence code;

    if (k.guid != "")
    {
        key.guid = k.guid;
        key.keycode = k.button & ~Qt::KeyboardModifierMask;
        key.held = held;
        key.ctrl = !!(k.button & Qt::ControlModifier);
        key.alt = !!(k.button & Qt::AltModifier);
        key.shift = !!(k.button & Qt::ShiftModifier);
    }
    else
    {
        if (k.keycode == "")
            code = QKeySequence(Qt::Key_unknown);
        else
            code = QKeySequence::fromString(k.keycode, QKeySequence::PortableText);

        Qt::KeyboardModifiers mods = Qt::NoModifier;
        if (code != Qt::Key_unknown)
            win_key::from_qt(code, idx, mods);

        key.guid = QStringLiteral("");
        key.keycode = idx;
        key.held = held;
        key.ctrl = !!(mods & Qt::ControlModifier);
        key.alt = !!(mods & Qt::AltModifier);
        key.shift = !!(mods & Qt::ShiftModifier);
    }
}
#endif

#ifdef _WIN32
void Shortcuts::receiver(const Key& k)
{
    const unsigned sz = keys.size();
    for (unsigned i = 0; i < sz; i++)
    {
        K& k_ = std::get<0>(keys[i]);
        if (k.guid != k_.guid)
            continue;
        if (k.keycode != k_.keycode)
            continue;

        if (k_.held && !k.held) continue;
        if (k_.alt != k.alt) continue;
        if (k_.ctrl != k.ctrl) continue;
        if (k_.shift != k.shift) continue;
        if (!k_.should_process())
            continue;

        fun& f = std::get<1>(keys[i]);
        f(k.held);
    }
}
#endif

void Shortcuts::reload(const t_keys& keys_)
{
    const unsigned sz = keys_.size();
    keys = std::vector<tt>();

    for (unsigned i = 0; i < sz; i++)
    {
        const auto& kk = keys_[i];
        const key_opts& opts = std::get<0>(kk);
        const bool held = std::get<2>(kk);
        auto fun = std::get<1>(kk);
        K k;
        bind_shortcut(k, opts, held);
        keys.push_back(tt(k, [=](unused_on_unix(bool, flag)) -> void
        {
#ifdef _WIN32
            fun(flag);
#else
            fun(true);
#endif
        },
        held));
#ifndef _WIN32
        const int idx = keys.size() - 1;
        tt& kk_ = keys[idx];
        auto& fn = std::get<1>(kk_);
        connect(k.get(), &QxtGlobalShortcut::activated, [=]() -> void { fn(true); });
#endif
    }
}
