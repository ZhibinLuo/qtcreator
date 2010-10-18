/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#if defined __cplusplus
#include <QtCore/QtGlobal>

#ifdef Q_WS_WIN
# define _POSIX_
# include <limits.h>
# undef _POSIX_
#endif

#include <botan/stream_cipher.h>
#include <botan/pubkey_enums.h>
#include <botan/filters.h>
#include <botan/libstate.h>
#include <botan/pubkey.h>
#include <botan/rotate.h>
#include <botan/util.h>
#include <botan/xor_buf.h>
#include <botan/look_pk.h>
#include <botan/mac.h>
#include <botan/secmem.h>
#include <botan/pipe.h>
#include <botan/oids.h>
#include <botan/exceptn.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>
#include <botan/types.h>
#include <botan/rng.h>
#include <botan/numthry.h>
#include <botan/bigint.h>
#include <botan/botan.h>
#include <botan/hash.h>
#include <botan/loadstor.h>
#include <botan/parsing.h>
#include <botan/block_cipher.h>

#include <map>
#include <fstream>
#include <memory>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>

#endif
