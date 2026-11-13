// ArgpiSuite JWT (JSON Web Token) Decoder & Verifier Service.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Tools {

struct JwtDecodeResult {
    bool ok = false;
    std::string headerJson;
    std::string payloadJson;
    std::string signatureB64Url;
    std::string signatureHex;
    std::string signingInput;  // "<header>.<payload>"
    std::string algorithm;     // e.g. "HS256", "RS256"
    std::wstring claimsSummary;
    std::wstring message;
};

struct JwtVerifyResult {
    bool attempted = false;
    bool supported = false;
    bool valid = false;
    std::wstring message;
};

class JwtDecoderService {
public:
    static JwtDecoderService& Instance();

    /// Decodes a JWT (optionally prefixed with "Bearer ").
    JwtDecodeResult Decode(std::string_view tokenInput);

    /// Verifies HS256, HS384, or HS512 signature using `secretUtf8`.
    JwtVerifyResult VerifyHmac(const JwtDecodeResult& decoded, std::string_view secretUtf8);
};

}  // namespace ArgpiSuite::Tools

