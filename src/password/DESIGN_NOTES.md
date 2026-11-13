# ArgpiSuite — Generate Password design notes (not implemented)

Reserved for the `ArgpiSuite::Password` module. The menus only show
"planned but not implemented yet"; nothing below is implemented yet.

- **CSPRNG only**: `BCryptGenRandom(nullptr, ..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)`.
  Rejection sampling for a uniform character distribution; no `rand()`, no
  modulo bias.
- **Policy** (`PasswordPolicy`): length, lower/upper/digits/symbols toggles,
  ambiguous-character filter (`Il1O0`), minimum digit and symbol counts.
  Validate that the requested minimums fit into the requested length.
- **Passphrases**: a bundled word list (public domain / permissive licence,
  listed in `THIRD_PARTY.md`) with CSPRNG index selection, separator and word
  count from settings.
- **Insertion**: `Generate Password` previews in a small dialog; `Insert` uses
  the same single-undo-step helper as the formatter; `Copy` uses the clipboard
  and clears it after a configurable timeout if the user opted in.
- **Memory hygiene**: wipe intermediate buffers with `SecureZeroMemory`, never
  log generated secrets, never store them in the INI file.
- **Settings** (`[Password]` in `ArgpiSuite.ini`): `length`, `useLower`,
  `useUpper`, `useDigits`, `useSymbols`, `avoidAmbiguous`, `minSymbols`,
  `minDigits`, `words`, `separator`.
- **UI**: the five reserved menu items already exist with stable command IDs,
  so implementing the generator never shifts user shortcuts.
