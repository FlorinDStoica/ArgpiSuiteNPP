#include "JwtDecoderService.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "crypto/EncodingService.h"
#include "format/Format.h"
#include "format/FormatLanguage.h"
#include "format/FormatOptions.h"
#include "format/json/FormatJsonParser.h"
#include "hash/HashService.h"
#include "password/CryptoHash.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

std::string TrimAscii(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

std::string StripBearerPrefix(std::string token) {
    if (token.size() >= 7) {
        std::string prefix = token.substr(0, 7);
        for (char& ch : prefix) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (prefix == "bearer ") {
            return TrimAscii(std::string_view(token).substr(7));
        }
    }
    return token;
}

std::string PrettyPrintJson(std::string_view rawJson) {
    Format::FormatOptions opts;
    opts.tabWidth = 2;
    opts.endOfLine = Format::Eol::Crlf;
    const Format::FormatResult formatted =
        Format::FormatText(rawJson, Format::Language::Json, opts);
    if (formatted.ok) {
        std::string out = formatted.output;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
            out.pop_back();
        }
        return out;
    }
    return std::string(rawJson);
}

std::wstring FormatUnixTimestamp(int64_t epochSec) {
    const std::time_t tt = static_cast<std::time_t>(epochSec);
    std::tm utcTm{};
    if (::gmtime_s(&utcTm, &tt) != 0) {
        return std::to_wstring(epochSec);
    }
    wchar_t buf[64] = {};
    if (::wcsftime(buf, sizeof(buf) / sizeof(buf[0]), L"%Y-%m-%d %H:%M:%S UTC", &utcTm) == 0) {
        return std::to_wstring(epochSec);
    }
    return std::wstring(buf);
}

std::wstring BuildClaimsSummary(std::string_view headerJson, std::string_view payloadJson,
                                std::string& outAlg) {
    std::wstring summary;
    const Format::JsonParseResult hdrParse = Format::ParseJson(headerJson, Format::Language::Json);
    if (hdrParse.ok && hdrParse.document.root != Format::kNoJsonNode) {
        const auto& root = hdrParse.document.Node(hdrParse.document.root);
        if (const auto* alg = Format::JsonMemberValue(hdrParse.document, root, "alg")) {
            outAlg = Format::JsonScalarText(*alg);
            summary += L"alg: " + Plugin::ToWide(outAlg);
        }
        if (const auto* typ = Format::JsonMemberValue(hdrParse.document, root, "typ")) {
            if (!summary.empty()) summary += L" | ";
            summary += L"typ: " + Plugin::ToWide(Format::JsonScalarText(*typ));
        }
    }

    const Format::JsonParseResult payParse = Format::ParseJson(payloadJson, Format::Language::Json);
    if (payParse.ok && payParse.document.root != Format::kNoJsonNode) {
        const auto& root = payParse.document.Node(payParse.document.root);
        auto appendStrClaim = [&](const char* key, const wchar_t* label) {
            if (const auto* node = Format::JsonMemberValue(payParse.document, root, key)) {
                if (!summary.empty()) summary += L"\r\n";
                summary += std::wstring(label) + L": " +
                           Plugin::ToWide(Format::JsonScalarText(*node));
            }
        };
        appendStrClaim("iss", L"iss");
        appendStrClaim("sub", L"sub");
        appendStrClaim("aud", L"aud");

        const int64_t nowSec = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        auto appendTimeClaim = [&](const char* key, const wchar_t* label, bool checkExpiry) {
            if (const auto* node = Format::JsonMemberValue(payParse.document, root, key)) {
                if (node->kind == Format::JsonKind::Number) {
                    try {
                        const int64_t ts = std::stoll(node->raw);
                        if (!summary.empty()) summary += L"\r\n";
                        summary += std::wstring(label) + L": " + FormatUnixTimestamp(ts);
                        if (checkExpiry) {
                            summary += (ts < nowSec) ? L" [EXPIRED]" : L" [ACTIVE]";
                        }
                    } catch (...) {
                    }
                }
            }
        };
        appendTimeClaim("iat", L"iat", false);
        appendTimeClaim("nbf", L"nbf", false);
        appendTimeClaim("exp", L"exp", true);
    }

    return summary;
}

}  // namespace

JwtDecoderService& JwtDecoderService::Instance() {
    static JwtDecoderService instance;
    return instance;
}

JwtDecodeResult JwtDecoderService::Decode(std::string_view tokenInput) {
    JwtDecodeResult result;
    std::string token = StripBearerPrefix(TrimAscii(tokenInput));
    if (token.empty()) {
        result.message = L"Token is empty. Paste or select a JWT (eyJ...).";
        return result;
    }

    const size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) {
        result.message = L"Invalid JWT: missing '.' separators.";
        return result;
    }
    const size_t dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos || token.find('.', dot2 + 1) != std::string::npos) {
        result.message = L"Invalid JWT: expected 3 dot-separated parts (header.payload.signature).";
        return result;
    }

    const std::string headerB64 = token.substr(0, dot1);
    const std::string payloadB64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    const std::string sigB64 = token.substr(dot2 + 1);

    Crypto::Base64Options b64Opts;
    b64Opts.urlSafe = true;
    b64Opts.padding = false;

    const Crypto::EncodingResult hdrRes =
        Crypto::EncodingService::Instance().DecodeBase64(headerB64, b64Opts);
    if (!hdrRes.ok) {
        result.message = L"Failed to decode JWT header (Base64URL): " + hdrRes.message;
        return result;
    }

    const Crypto::EncodingResult payRes =
        Crypto::EncodingService::Instance().DecodeBase64(payloadB64, b64Opts);
    if (!payRes.ok) {
        result.message = L"Failed to decode JWT payload (Base64URL): " + payRes.message;
        return result;
    }

    result.headerJson = PrettyPrintJson(hdrRes.text);
    result.payloadJson = PrettyPrintJson(payRes.text);
    result.signatureB64Url = sigB64;
    result.signingInput = token.substr(0, dot2);

    if (!sigB64.empty()) {
        const Crypto::EncodingResult sigRes =
            Crypto::EncodingService::Instance().DecodeBase64(sigB64, b64Opts);
        if (sigRes.ok) {
            result.signatureHex =
                Password::BytesToHex(sigRes.bytes.data(), sigRes.bytes.size());
        }
    }

    result.claimsSummary =
        BuildClaimsSummary(hdrRes.text, payRes.text, result.algorithm);
    result.ok = true;
    return result;
}

JwtVerifyResult JwtDecoderService::VerifyHmac(const JwtDecodeResult& decoded,
                                              std::string_view secretUtf8) {
    JwtVerifyResult vr;
    if (!decoded.ok) {
        vr.message = L"Cannot verify an invalid JWT.";
        return vr;
    }

    Hash::HashAlgorithm alg = Hash::HashAlgorithm::HmacSha256;
    if (decoded.algorithm == "HS256") {
        alg = Hash::HashAlgorithm::HmacSha256;
        vr.supported = true;
    } else if (decoded.algorithm == "HS384") {
        alg = Hash::HashAlgorithm::HmacSha384;
        vr.supported = true;
    } else if (decoded.algorithm == "HS512") {
        alg = Hash::HashAlgorithm::HmacSha512;
        vr.supported = true;
    } else {
        vr.supported = false;
        vr.message = L"HMAC verification only supports HS256, HS384, and HS512 (token uses '" +
                     Plugin::ToWide(decoded.algorithm.empty() ? "unknown" : decoded.algorithm) +
                     L"').";
        return vr;
    }

    vr.attempted = true;
    Hash::HashOptions hashOpts;
    hashOpts.algorithm = alg;
    hashOpts.hmacKeyUtf8 = std::string(secretUtf8);

    const Hash::HashResult mac =
        Hash::HashService::Instance().Compute(decoded.signingInput, hashOpts);
    if (!mac.ok) {
        vr.message = mac.message;
        return vr;
    }

    Crypto::Base64Options b64Opts;
    b64Opts.urlSafe = true;
    b64Opts.padding = false;
    const Crypto::EncodingResult enc =
        Crypto::EncodingService::Instance().EncodeBase64(mac.rawBytes, b64Opts);
    if (!enc.ok) {
        vr.message = enc.message;
        return vr;
    }

    vr.valid = Password::ConstantTimeCompare(enc.text, decoded.signatureB64Url);
    vr.message = vr.valid ? L"Signature VALID" : L"Signature INVALID";
    return vr;
}

}  // namespace ArgpiSuite::Tools
