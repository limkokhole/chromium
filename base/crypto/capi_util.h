// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CRYPTO_CAPI_UTIL_H_
#define BASE_CRYPTO_CAPI_UTIl_H_

#include <windows.h>
#include <wincrypt.h>

namespace base {

// CryptAcquireContext when passed CRYPT_NEWKEYSET or CRYPT_DELETEKEYSET in
// flags is not thread-safe. For such calls, we create a global lock to
// synchronize it.
//
// From "Threading Issues with Cryptographic Service Providers",
// <http://msdn.microsoft.com/en-us/library/aa388149(v=VS.85).aspx>:
//
// "The CryptAcquireContext function is generally thread safe unless
// CRYPT_NEWKEYSET or CRYPT_DELETEKEYSET is specified in the dwFlags
// parameter."
BOOL CryptAcquireContextLocked(HCRYPTPROV* prov,
                               const TCHAR* container,
                               const TCHAR* provider,
                               DWORD prov_type,
                               DWORD flags);

}  // namespace base

#endif  // BASE_CRYPTO_CAPI_UTIl_H_
