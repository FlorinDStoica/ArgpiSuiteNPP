# ArgpiSuite — Encrypt / Decrypt design notes (not implemented)

Reserved for the `ArgpiSuite::Crypto` module. The menus only show
"planned but not implemented yet"; nothing below is implemented yet.

- **Primitive**: AES-256-GCM through Windows CNG (`BCryptEncrypt` /
  `BCryptDecrypt`). No third-party crypto library, no external process.
- **KDF**: PBKDF2-HMAC-SHA256 via `BCryptDeriveKeyPBKDF2` initially; keep the
  algorithm and iteration count in the container so Argon2id can be added
  later without a format break.
- **Container**: versioned binary header — magic `ARGPICR1`, version, cipher id,
  KDF id, iteration count, salt length + salt, nonce length + nonce, then
  ciphertext + GCM tag. All integers little-endian and fixed width.
- **Randomness**: `BCryptGenRandom(nullptr, ..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)`.
  Never `rand()`, never a timestamp seed.
- **Memory hygiene**: wipe passphrases, derived keys and plaintext buffers with
  `SecureZeroMemory`; avoid `std::wstring` copies of secrets where possible.
  Never write secrets to the Output Log or the INI file.
- **Undo / editor**: Scintilla undo history keeps previous text. For document
  encryption, offer "save to file" rather than replacing the buffer, or clear
  the undo history (`SCI_EMPTYUNDOBUFFER`) after the operation and say so.
- **Files**: write through a temporary file and `ReplaceFileW`; never leave a
  half-written ciphertext. Optional `.bak` must itself never contain plaintext
  unless the user explicitly asks for it.
- **Settings** (`[Crypto]` in `ArgpiSuite.ini`): `enabled`, `cipher`, `kdf`,
  `iterations`. Import/export of keys is out of scope for the first version.
- **Failure policy**: authentication failures must report "wrong passphrase or
  corrupted file" and must not output partial plaintext.
