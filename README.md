# ArgpiSuiteNPP

**ArgpiSuiteNPP** is a native C++17 all-in-one developer plugin for **Notepad++** (x64 and x86). It runs entirely inside `ArgpiSuite.dll` with **zero external runtime dependencies**—no Node.js, npm, Python, or external CLI tools are required, relying only on the Microsoft C++ standard library, the Windows SDK, and Windows CNG (`bcrypt`).

> **Note — First Release & Bug Reports:**  
> This is the **first release** of **ArgpiSuiteNPP** and may contain bugs. If you encounter any issues, crashes, or unexpected formatting/tool behavior, please report them on [GitHub Issues](https://github.com/FlorinDStoica/ArgpiSuiteNPP/issues) so they can be investigated and fixed.

**Repository:** <https://github.com/FlorinDStoica/ArgpiSuiteNPP.git>

---

## Features

ArgpiSuiteNPP adds seven tool groups (plus an **About…** dialog at the bottom) under **`Plugins > ArgpiSuiteNPP`**:

| Submenu / Item | Capabilities |
|---|---|
| **Format** | Code formatting, minification, check-only mode, format-on-save, batch open documents & folder formatting |
| **Encrypt / Decrypt** | Authenticated symmetric encryption/decryption for selections, documents, and files |
| **Encode / Decode** | Reversible Base16, Base64, Base85, and custom-alphabet encoding/decoding |
| **Password Generator** | Cryptographic password & Diceware passphrase generation with entropy analysis |
| **UUID / GUID & ULID Generator** | Instant and bulk generation of UUID (v4, v7), GUID, and ULID identifiers |
| **Hashing** | Cryptographic digests, double hashes, HMACs, and CRC32 for text and files |
| **Tools** | JWT decoder/verifier, JSON/XML/SQL path querying, CSV/TSV alignment & filtering, smart deduplication & sorting |
| **About…** | Displays version, credits, license info, and the GitHub repository URL |

---

### 1. Format

Native Prettier-style formatter with automatic language detection and JSON sniffing for unsaved or `Normal text` buffers.

* **Supported languages:** JSON, JSON5, JSONC, CSS, SCSS, Less, JavaScript, TypeScript, JSX, TSX, HTML, Vue, XML, PHP, Java, Kotlin, Gradle, YAML, Markdown, C, C++.

| Command | Shortcut | Description |
|---|---|---|
| **Format Document** | `Ctrl+Alt+Shift+P` | Formats the active buffer in a single undo step |
| **Format Selection** | | Formats the selection (or the whole document if empty) |
| **Minify Document** | | Minifies the active document |
| **Check Formatting** | | Reports whether the buffer would change without editing it |
| **Toggle Format on Save** | | Checkable toggle; formats supported files on save (`NPPN_FILEBEFORESAVE`) |
| **Format All Open Documents** | | Formats and saves all open, supported, saved files |
| **Format Folder…** | | Recursive folder formatter with worker thread, progress UI, and Cancel |
| **Show Resolved Config** | | Prints effective options, resolved config path, and notes to the log |
| **Format Settings…** | | Edits the `[Format]` section in `ArgpiSuite.ini` |
| **Show Format Log** | | Opens the modeless output log |

#### Configuration & Safety
* Settings are stored in `%NPP_PLUGINSCONFIGDIR%\ArgpiSuite.ini` (UTF-16), created with defaults on first run.
* Per-document options are resolved by walking up from the document directory:
  1. `.editorconfig` (`indent_style`, `indent_size`, `tab_width`, `max_line_length`, `end_of_line`)
  2. Nearest `.formatrc*` / `.prettierrc*` (`.json`, `.json5`, `.jsonc`, or extension-less) or `package.json#format` *(JS/YAML config files are never executed and are logged as skipped)*
  3. `[Format]` INI settings as fallback
* `.formatignore` and `.prettierignore` are respected across single-document, open-document, and folder commands.
* **Safety guarantees:** Syntax errors, unsupported languages, read-only buffers, equivalence mismatches, and timeouts never modify the buffer. Unchanged output never creates an undo entry or dirty flag. A cooperative watchdog (`timeoutMs`, default 5 s) protects the UI thread.

---

### 2. Encrypt / Decrypt

Authenticated encryption and decryption backed by Windows CNG (`bcrypt`):

* **Ciphers:** `AES-256-GCM`, `AES-256-CBC`, `AES-128-GCM`, `AES-128-CBC`
* **Key derivation (KDF):** `PBKDF2-HMAC-SHA256` (default 600,000 iterations) and `Argon2id`
* **Format:** Versioned binary container (`ARGPICR1`) with optional Base64 text armor
* **Commands:** Encrypt Selection, Decrypt Selection, Encrypt Document, Decrypt Document, Encrypt File…, Decrypt File…, Encryption Settings…

---

### 3. Encode / Decode

Pure reversible data encoding and decoding (no key required):

* **Base16 (Hexadecimal):** Uppercase/lowercase with optional delimiters (` `, `:`, `-`)
* **Base64:** RFC 4648 standard and URL-safe, configurable padding and line wrapping (64/76)
* **Base85:** Ascii85 (Adobe), Z85 (ZeroMQ), and RFC 1924
* **Custom Base:** Arbitrary user-defined alphabets (e.g., Base58, Base62)

---

### 4. Password Generator

* **Passwords & Passphrases:** Complexity profiles (`Basic`, `Balanced`, `Strong`, `Elite`), custom character classes, ambiguous-character exclusion, and Diceware passphrase generation
* **Analysis & Hashing:** Strength/entropy calculation and password hashing/verification (`BCrypt`, `Argon2id`, `SHA-256`, `SHA-512`, `MD5`, `SHA-1`, `PBKDF2-SHA256`)
* **Commands:** Generate Password, Generate Passphrase, Insert at Caret, Copy to Clipboard, Password Settings…

---

### 5. UUID / GUID & ULID Generator

* **UUID v4** (RFC 9562 cryptographically random)
* **UUID v7** (RFC 9562 Unix-epoch millisecond timestamp + monotonic counter)
* **GUID** (Microsoft-style UUID formatting)
* **ULID** (48-bit timestamp + 80-bit monotonic randomness in Crockford Base32)
* **Bulk / Custom Generator…** for batch generation with configurable quoting and separators

---

### 6. Hashing

Compute digests and checksums over selections, documents, or files on disk:

* **Quick menu items:** MD5, SHA-256, SHA-512, 2xSHA-256 (Double SHA-256), HMAC-SHA256…, CRC32
* **Hash & Checksum Tool…:** Full dialog supporting MD5, SHA-1, SHA-256, Double SHA-256, SHA-384, SHA-512, BLAKE2b-256, BLAKE2b-512, CRC32, and HMAC (MD5, SHA-1, SHA-256, SHA-384, SHA-512) with UTF-8 or hex keys

---

### 7. Tools

* **JWT (JSON Web Token) Decoder:** Decodes header, payload, signature (Base64Url & Hex), and claims summary (supports `Bearer ` prefix), plus HS256 / HS384 / HS512 signature verification.
* **JSON / XML / SQL Path Querying:** Auto-detects document format and evaluates JSONPath, XPath, or SQL `INSERT` queries.
* **CSV / TSV Column Alignment & Filtering:** Auto-detects delimiters (`,`, `\t`, `;`, `|`), aligns or compacts columns, extracts/deletes columns by index or header name, and filters rows.
* **Smart Deduplication & Sorting:** Deduplicate lines while preserving order, sort alphabetically / by length / natural numeric, sort JSON keys alphabetically, remove empty lines, or reverse lines.

---

## Installation

### Requirements
* **Notepad++ 8.4 or later** (64-bit or 32-bit) on Windows

### Manual Installation
1. Download or build `ArgpiSuite.dll` matching your Notepad++ architecture (`x64` from `argpisuitenpp_x64\ArgpiSuite.dll` or `x86` from `argpisuitenpp_x32\ArgpiSuite.dll`).
2. Create a folder named `ArgpiSuite` inside your Notepad++ `plugins` directory:
   ```text
   <Notepad++>\plugins\ArgpiSuite\ArgpiSuite.dll
   ```
3. Restart Notepad++. The plugin appears under **`Plugins > ArgpiSuiteNPP`**.

---

## Building from Source

### Prerequisites
* **Visual Studio 2022** (or newer) with the **Desktop development with C++** workload
* **CMake 3.20+** (for running the unit test suite, benchmark, and CLI)

### Build the Notepad++ Plugin (MSBuild)

From a Visual Studio Developer Command Prompt (or after calling `VsDevCmd.bat`):

```bat
:: 64-bit (Release|x64) -> bin64\ArgpiSuite.dll
msbuild vs.proj\ArgpiSuite.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal

:: 32-bit (Release|Win32) -> bin\ArgpiSuite.dll
msbuild vs.proj\ArgpiSuite.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:minimal
```

---

## Documentation & Third-Party Notices

* [`KNOWN_DIFFERENCES.md`](KNOWN_DIFFERENCES.md) — Formatter language coverage and differences from Prettier 3.x
* [`THIRD_PARTY.md`](THIRD_PARTY.md) — Development-time tools and Notepad++ plugin header notices

---

## License

This project is open source under the **MIT License** — anyone is free to copy, modify, and redistribute it, with no warranty of any kind. See [`license.txt`](license.txt) for the full license text.

*(Note: The Notepad++ plugin interface headers included for building against Notepad++ follow the terms noted in [`THIRD_PARTY.md`](THIRD_PARTY.md).)*
