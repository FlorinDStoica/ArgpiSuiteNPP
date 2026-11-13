#include "PasswordService.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "CryptoHash.h"

namespace ArgpiSuite::Password {
namespace {

const wchar_t kLower[] = L"abcdefghijklmnopqrstuvwxyz";
const wchar_t kUpper[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const wchar_t kDigits[] = L"0123456789";
const wchar_t kSymbols[] = L"!@#$%^&*()-_=+[]{};:,.<>?/|~";
const wchar_t kAmbiguous[] = L"0O1Il|'\"`";

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                           static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          wide.data(), size);
    return wide;
}

// Curated Diceware passphrase wordlist
const wchar_t* const kWordlist[] = {
    L"ability", L"absorb", L"abstract", L"academy", L"accent", L"acoustic", L"action", L"active",
    L"adapt", L"advance", L"adventure", L"aerobic", L"afford", L"agent", L"agree", L"airport",
    L"alert", L"alien", L"alley", L"almond", L"alpine", L"altitude", L"amateur", L"amber",
    L"anchor", L"ancient", L"android", L"angel", L"antenna", L"antique", L"apple", L"apron",
    L"arcade", L"arch", L"arctic", L"arena", L"armor", L"arrow", L"artist", L"aspect",
    L"asset", L"athlete", L"atlas", L"atom", L"atomic", L"audio", L"autumn", L"avatar",
    L"avocado", L"badge", L"baker", L"ballet", L"bamboo", L"banana", L"banner", L"baron",
    L"barrel", L"basket", L"beacon", L"beauty", L"binary", L"biology", L"bird", L"biscuit",
    L"blade", L"blanket", L"blender", L"blossom", L"blueprint", L"board", L"bonus", L"border",
    L"bottle", L"boulder", L"bounce", L"breeze", L"bridge", L"bronze", L"bubble", L"buffer",
    L"bullet", L"bundle", L"cabin", L"cable", L"cactus", L"camera", L"campus", L"canal",
    L"candle", L"canvas", L"canyon", L"captain", L"carbon", L"carpet", L"carrot", L"castle",
    L"casual", L"cathedral", L"cedar", L"celestial", L"cement", L"census", L"century", L"cereal",
    L"chain", L"chalk", L"champion", L"channel", L"chapter", L"charcoal", L"chart", L"cherry",
    L"chess", L"chime", L"chimney", L"cinema", L"circle", L"circuit", L"citizen", L"citrus",
    L"clarity", L"classic", L"cliff", L"climate", L"clinic", L"clover", L"cluster", L"coastal",
    L"cobalt", L"coffee", L"cognac", L"coin", L"colony", L"comet", L"compass", L"complex",
    L"concept", L"concert", L"copper", L"coral", L"corner", L"cosmic", L"cotton", L"courage",
    L"cradle", L"craft", L"crater", L"crayon", L"creek", L"crescent", L"cricket", L"crimson",
    L"crown", L"crystal", L"cube", L"current", L"curtain", L"custom", L"cyber", L"dagger",
    L"dancer", L"daylight", L"deck", L"decor", L"delta", L"denim", L"desert", L"desktop",
    L"device", L"diamond", L"diary", L"diesel", L"digital", L"dignity", L"diner", L"dinosaur",
    L"diploma", L"direct", L"discover", L"diver", L"dock", L"doctor", L"dolphin", L"domain",
    L"donkey", L"donor", L"dragon", L"drawer", L"drift", L"driver", L"drum", L"duck",
    L"dune", L"duet", L"dynamo", L"eagle", L"early", L"earth", L"echo", L"eclipse",
    L"ecology", L"edge", L"edition", L"elastic", L"elbow", L"elder", L"electric", L"element",
    L"elephant", L"elevator", L"elite", L"emerald", L"empire", L"engine", L"enigma", L"enzyme",
    L"epoch", L"equator", L"erosion", L"escape", L"essay", L"estate", L"eternal", L"ether",
    L"ethics",L"evergreen", L"exhibit", L"exotic", L"expert", L"fabric", L"falcon", L"family",
    L"famous", L"fantasy", L"farm", L"fashion", L"feather", L"feature", L"federal", L"fender",
    L"festival", L"fiber", L"field", L"filter", L"finite", L"firefly", L"flavor", L"fleece",
    L"flight", L"flint", L"floral", L"flute", L"focus", L"fog", L"foil", L"forest",
    L"forge", L"fossil", L"fountain", L"fox", L"fragment", L"frame", L"freedom", L"fresco",
    L"frigate", L"frost", L"galaxy", L"gallery", L"garden", L"garlic", L"garnet", L"gateway",
    L"gauge", L"gear", L"gemstone", L"general", L"genius", L"gentle", L"glacier", L"glance",
    L"glide", L"glimpse", L"globe", L"glorious", L"glow", L"goblet", L"golden", L"gondola",
    L"gopher", L"gospel", L"grain", L"granite", L"grape", L"graphic", L"gravity", L"grid",
    L"groove", L"guitar", L"gulf", L"habitat", L"halibut", L"hallway", L"hammer", L"hamster",
    L"harbor", L"harmony", L"harvest", L"haven", L"hazard", L"headlight", L"health", L"heart",
    L"heaven", L"hedge", L"helix", L"helmet", L"herald", L"hero", L"highland", L"highway",
    L"historian", L"hockey", L"holiday", L"holly", L"honey", L"horizon", L"horn", L"horse",
    L"hospital", L"hostel", L"hound", L"house", L"humming", L"humor", L"hunter", L"hyacinth",
    L"hybrid", L"iceberg", L"icon", L"ideal", L"iguana", L"illusion", L"image", L"impact",
    L"impulse", L"index", L"indigo", L"infant", L"infinite", L"inherit", L"inquiry", L"insight",
    L"inspector", L"instant", L"intact", L"interval", L"inventor", L"island", L"ivory", L"jacket",
    L"jaguar", L"jasmine", L"javelin", L"jewel", L"journey", L"jubilee", L"judge", L"jungle",
    L"jupiter", L"juror", L"justice", L"kayak", L"kernel", L"kettle", L"keypad", L"kinetic",
    L"kingdom", L"kitchen", L"kiwi", L"knight", L"koala", L"ladder", L"lagoon", L"lamp",
    L"lance", L"lantern", L"laptop", L"lark", L"laser", L"latitude", L"laurel", L"lava",
    L"leader", L"legacy", L"legend", L"lemon", L"leopard", L"lesson", L"letter", L"lever",
    L"liberty", L"library", L"lichen", L"light", L"lily", L"limestone", L"linear", L"linguist",
    L"lion", L"liquid", L"lizard", L"llama", L"lobster", L"logic", L"lotus", L"lumber",
    L"lunar", L"lynx", L"magnet", L"magpie", L"mahogany", L"maker", L"mammoth", L"mandate",
    L"mango", L"mansion", L"manual", L"maple", L"marble", L"marine", L"market", L"marmot",
    L"marshal", L"matrix", L"meadow", L"medal", L"melody", L"memory", L"mentor", L"mercury",
    L"merit", L"meteor", L"micro", L"miller", L"mimic", L"mineral", L"mirror", L"mission",
    L"moat", L"modem", L"module", L"monarch", L"monument", L"moonlight", L"mosaic", L"mountain",
    L"museum", L"music", L"mustang", L"mystic", L"native", L"nature", L"nebula", L"nectar",
    L"needle", L"neon", L"nest", L"network", L"neutron", L"nexus", L"nickel", L"night",
    L"ninja", L"nitrogen", L"noble", L"nomad", L"northern", L"notebook", L"nova", L"novel",
    L"nugget", L"nursery", L"oasis", L"obelisk", L"observant", L"ocean", L"octave", L"octopus",
    L"olive", L"olympic", L"omega", L"onion", L"onyx", L"opal", L"opera", L"optic",
    L"orbit", L"orchard", L"orchid", L"origami", L"otter", L"outdoors", L"oval", L"oxygen",
    L"pacific", L"paddle", L"palace", L"palette", L"palm", L"panther", L"parade", L"paradise",
    L"parchment", L"parlor", L"parrot", L"passage", L"pastel", L"pasture", L"patent", L"pathway",
    L"patient", L"patrol", L"pavilion", L"peacock", L"pelican", L"pendulum", L"penguin", L"pepper",
    L"peppermint", L"perch", L"perfume", L"periodic", L"petal", L"phantom", L"phoenix", L"photo",
    L"piano", L"picnic", L"picture", L"pigeon", L"pillar", L"pilot", L"pine", L"pinnacle",
    L"pioneer", L"pipeline", L"pirate", L"pistol", L"planet", L"plasma", L"plateau", L"plaza",
    L"plover", L"plume", L"pocket", L"poetry", L"polar", L"polygon", L"poplar", L"portal",
    L"portrait", L"postage", L"pottery", L"powder", L"prairie", L"prism", L"prodigy", L"profile",
    L"projector", L"prophet", L"pulse", L"puma", L"pyramid", L"quantum", L"quartz", L"quasar",
    L"queen", L"quill", L"quiver", L"rabbit", L"radar", L"radiant", L"radical", L"radius",
    L"rainbow", L"ranch", L"ranger", L"rapid", L"raven", L"reactor", L"rebel", L"recipe",
    L"record", L"redwood", L"reed", L"refinery", L"refuge", L"regal", L"relief", L"relic",
    L"remote", L"render", L"reptile", L"resin", L"resolute", L"resource", L"retro", L"ribbon",
    L"ridge", L"rifle", L"ripple", L"ritual", L"river", L"roadway", L"rooster", L"rosewood",
    L"rover", L"ruby", L"runway", L"saber", L"safari", L"sage", L"sailor", L"sakura",
    L"salmon", L"saloon", L"salute", L"sample", L"sandal", L"sandstone", L"sapphire", L"satellite",
    L"saucer", L"savanna", L"scanner", L"scenic", L"scholar", L"science", L"scooter", L"scout",
    L"screen", L"scribe", L"scroll", L"sculpture", L"seaside", L"season", L"secret", L"sedan",
    L"sensor", L"sequoia", L"serenity", L"serpent", L"shadow", L"shampoo", L"shield", L"shining",
    L"shipwreck", L"shoreline", L"shuttle", L"signal", L"silicon", L"silver", L"siren", L"skater",
    L"skeleton", L"skylight", L"skyscraper", L"slate", L"sleigh", L"slogan", L"slot", L"small",
    L"smart", L"snowdrop", L"solar", L"soldier", L"solitude", L"sonic", L"sound", L"sparrow",
    L"spectrum", L"sphere", L"sphinx", L"spider", L"spiral", L"splash", L"sponsor", L"spring",
    L"spruce", L"squad", L"squirrel", L"stadium", L"stage", L"stalactite", L"stand", L"starfish",
    L"station", L"statue", L"steamboat", L"stellar", L"step", L"stereo", L"steward", L"sticker",
    L"stone", L"storm", L"strand", L"stream", L"strata", L"structure", L"studio", L"subway",
    L"sugar", L"suite", L"summit", L"sunburst", L"sunflower", L"sunrise", L"sunset", L"surgeon",
    L"swift", L"symbol", L"syrup", L"tablet", L"tackle", L"tailor", L"talent", L"tamarind",
    L"tandem", L"tapestry", L"target", L"tariff", L"tavern", L"teacup", L"teal", L"telescope",
    L"temple", L"tenant", L"terrace", L"theory", L"thermal", L"thistle", L"thunder", L"timber",
    L"titan", L"toast", L"tobacco", L"token", L"tomato", L"topaz", L"torch", L"tornado",
    L"torpedo", L"tourist", L"tower", L"tractor", L"traffic", L"trail", L"trance", L"transit",
    L"trapeze", L"treetop", L"tribute", L"trident", L"trigger", L"trophy", L"tropical", L"trumpet",
    L"tulip", L"tundra", L"tunnel", L"turbine", L"turtle", L"twilight", L"typhoon", L"ultra",
    L"umbrella", L"unicorn", L"unique", L"universe", L"uphill", L"uranium", L"urban", L"vacuum",
    L"valiant", L"valley", L"valve", L"vanilla", L"vapor", L"vector", L"velvet", L"venture",
    L"venus", L"veranda", L"vertex", L"vessel", L"veteran", L"viaduct", L"vibrant", L"vicar",
    L"victor", L"village", L"vineyard", L"vintage", L"violin", L"virtue", L"vision", L"visual",
    L"vocal", L"volcano", L"vortex", L"voyage", L"vulture", L"waffle", L"walnut", L"wanderer",
    L"wardrobe", L"warrior", L"waterfall", L"wave", L"weapon", L"weasel", L"weather", L"webcam",
    L"welcome", L"western", L"whale", L"wheat", L"whisper", L"whistle", L"willow", L"windmill",
    L"window", L"winter", L"wisdom", L"wizard", L"wolf", L"wombat", L"woodland", L"wool",
    L"workshop", L"wren", L"yacht", L"yellow", L"yucca", L"zebra", L"zenith", L"zephyr",
    L"zodiac", L"zone"
};

constexpr size_t kWordlistSize = sizeof(kWordlist) / sizeof(kWordlist[0]);

}  // namespace

PasswordPolicy PolicyForComplexity(Complexity complexity) {
    PasswordPolicy p;
    switch (complexity) {
        case Complexity::Basic:
            p.length = 12;
            p.useLower = true;
            p.useUpper = false;
            p.useDigits = false;
            p.useSymbols = false;
            p.avoidAmbiguous = false;
            break;
        case Complexity::Balanced:
            p.length = 16;
            p.useLower = true;
            p.useUpper = true;
            p.useDigits = true;
            p.useSymbols = false;
            p.avoidAmbiguous = false;
            break;
        case Complexity::Strong:
            p.length = 20;
            p.useLower = true;
            p.useUpper = true;
            p.useDigits = true;
            p.useSymbols = true;
            p.avoidAmbiguous = false;
            break;
        case Complexity::Elite:
            p.length = 32;
            p.useLower = true;
            p.useUpper = true;
            p.useDigits = true;
            p.useSymbols = true;
            p.avoidAmbiguous = true;
            break;
    }
    return p;
}

std::wstring ComplexityName(Complexity complexity) {
    switch (complexity) {
        case Complexity::Basic: return L"basic";
        case Complexity::Balanced: return L"balanced";
        case Complexity::Strong: return L"strong";
        case Complexity::Elite: return L"elite";
    }
    return L"balanced";
}

Complexity ParseComplexity(const std::wstring& name) {
    if (name == L"basic") return Complexity::Basic;
    if (name == L"strong") return Complexity::Strong;
    if (name == L"elite") return Complexity::Elite;
    return Complexity::Balanced;
}

void ApplyPreset(Preset preset, Algorithm& alg, std::wstring& secretKey) {
    switch (preset) {
        case Preset::Custom:
            alg = Algorithm::CustomBcrypt;
            secretKey.clear();
            break;
        case Preset::Joomla:
            alg = Algorithm::JoomlaBcrypt;
            secretKey.clear();
            break;
        case Preset::WordPress:
            alg = Algorithm::CustomBcrypt;
            secretKey = L"WordPress-Reset-Secret";
            break;
        case Preset::Drupal:
            alg = Algorithm::Sha256Secret;
            secretKey = L"Drupal-Reset-Secret";
            break;
        case Preset::Magento:
            alg = Algorithm::CustomBcrypt;
            secretKey = L"Magento-Reset-Secret";
            break;
    }
}

std::wstring PresetName(Preset preset) {
    switch (preset) {
        case Preset::Custom: return L"custom";
        case Preset::Joomla: return L"joomla";
        case Preset::WordPress: return L"wordpress";
        case Preset::Drupal: return L"drupal";
        case Preset::Magento: return L"magento";
    }
    return L"custom";
}

Preset MatchPreset(Algorithm alg, const std::wstring& secretKey) {
    if (alg == Algorithm::JoomlaBcrypt && secretKey.empty()) {
        return Preset::Joomla;
    }
    if (alg == Algorithm::CustomBcrypt && secretKey == L"WordPress-Reset-Secret") {
        return Preset::WordPress;
    }
    if (alg == Algorithm::Sha256Secret && secretKey == L"Drupal-Reset-Secret") {
        return Preset::Drupal;
    }
    if (alg == Algorithm::CustomBcrypt && secretKey == L"Magento-Reset-Secret") {
        return Preset::Magento;
    }
    return Preset::Custom;
}

std::wstring AlgorithmName(Algorithm alg) {
    switch (alg) {
        case Algorithm::JoomlaBcrypt: return L"joomla_bcrypt";
        case Algorithm::CustomBcrypt: return L"bcrypt";
        case Algorithm::Argon2id: return L"argon2id";
        case Algorithm::Sha256: return L"sha256";
        case Algorithm::Sha512: return L"sha512";
        case Algorithm::Md5: return L"md5";
        case Algorithm::Sha1: return L"sha1";
        case Algorithm::Sha256Secret: return L"sha256_secret";
        case Algorithm::Pbkdf2Secret: return L"pbkdf2_secret";
    }
    return L"joomla_bcrypt";
}

std::wstring AlgorithmDisplayName(Algorithm alg) {
    switch (alg) {
        case Algorithm::JoomlaBcrypt: return L"Joomla BCrypt";
        case Algorithm::CustomBcrypt: return L"Custom BCrypt";
        case Algorithm::Argon2id: return L"Argon2id";
        case Algorithm::Sha256: return L"SHA-256";
        case Algorithm::Sha512: return L"SHA-512";
        case Algorithm::Md5: return L"MD5 (legacy)";
        case Algorithm::Sha1: return L"SHA-1 (legacy)";
        case Algorithm::Sha256Secret: return L"SHA-256 with Secret Key";
        case Algorithm::Pbkdf2Secret: return L"PBKDF2 with Secret Key";
    }
    return L"Joomla BCrypt";
}

std::wstring AlgorithmDescription(Algorithm alg) {
    switch (alg) {
        case Algorithm::JoomlaBcrypt:
            return L"BCrypt with cost 10, the format Joomla uses. Very secure and intentionally slow to resist brute-force attacks.";
        case Algorithm::CustomBcrypt:
            return L"Custom BCrypt with configurable cost. Industry standard for password hashing. Very secure.";
        case Algorithm::Argon2id:
            return L"Argon2id memory-hard algorithm. Modern and resistant to GPU and ASIC attacks. Recommended for new systems.";
        case Algorithm::Sha256:
            return L"SHA-256 one-way hash. Fast and not designed for passwords. Use only for legacy compatibility.";
        case Algorithm::Sha512:
            return L"SHA-512 one-way hash. Larger output than SHA-256 but still not designed for passwords. Legacy use only.";
        case Algorithm::Md5:
            return L"MD5 hash function. Cryptographically broken. Only use for legacy compatibility.";
        case Algorithm::Sha1:
            return L"SHA-1 hash function. Deprecated. Use only for legacy system compatibility.";
        case Algorithm::Sha256Secret:
            return L"SHA-256 over the secret key and password. Adds a secret layer. Fast, so prefer PBKDF2 for new work.";
        case Algorithm::Pbkdf2Secret:
            return L"PBKDF2-SHA256 with 200,000 iterations, using the secret key as salt. Designed for password hashing. Very secure.";
    }
    return {};
}

Algorithm ParseAlgorithm(const std::wstring& name) {
    if (name == L"bcrypt") return Algorithm::CustomBcrypt;
    if (name == L"argon2id") return Algorithm::Argon2id;
    if (name == L"sha256") return Algorithm::Sha256;
    if (name == L"sha512") return Algorithm::Sha512;
    if (name == L"md5") return Algorithm::Md5;
    if (name == L"sha1") return Algorithm::Sha1;
    if (name == L"sha256_secret") return Algorithm::Sha256Secret;
    if (name == L"pbkdf2_secret") return Algorithm::Pbkdf2Secret;
    return Algorithm::JoomlaBcrypt;
}

int EstimatePoolSize(const std::wstring& password) {
    bool hasLower = false;
    bool hasUpper = false;
    bool hasDigit = false;
    bool hasSymbol = false;
    for (wchar_t ch : password) {
        if (ch >= L'a' && ch <= L'z') hasLower = true;
        else if (ch >= L'A' && ch <= L'Z') hasUpper = true;
        else if (ch >= L'0' && ch <= L'9') hasDigit = true;
        else hasSymbol = true;
    }
    int size = 0;
    if (hasLower) size += 26;
    if (hasUpper) size += 26;
    if (hasDigit) size += 10;
    if (hasSymbol) size += 33;
    return size;
}

double EstimateEntropy(const std::wstring& password, int poolSize) {
    if (password.empty()) {
        return 0.0;
    }
    const int size = std::max(poolSize > 0 ? poolSize : EstimatePoolSize(password), 2);
    return static_cast<double>(password.size()) * (std::log2(static_cast<double>(size)));
}

std::wstring StrengthRating(double entropyBits) {
    if (entropyBits >= 96.0) return L"Elite";
    if (entropyBits >= 72.0) return L"Strong";
    if (entropyBits >= 52.0) return L"Moderate";
    if (entropyBits >= 28.0) return L"Weak";
    return L"Very Weak";
}

PasswordService& PasswordService::Instance() {
    static PasswordService instance;
    return instance;
}

PasswordResult PasswordService::Generate(const PasswordPolicy& policy) {
    PasswordResult result;
    const int len = std::min(4096, std::max(4, policy.length));

    std::unordered_set<wchar_t> excluded;
    for (wchar_t ch : policy.excludeChars) {
        excluded.insert(ch);
    }
    if (policy.avoidAmbiguous) {
        for (const wchar_t* p = kAmbiguous; *p != L'\0'; ++p) {
            excluded.insert(*p);
        }
    }

    auto filterSet = [&](const wchar_t* src) -> std::wstring {
        std::wstring s;
        for (const wchar_t* p = src; *p != L'\0'; ++p) {
            if (excluded.find(*p) == excluded.end()) {
                s.push_back(*p);
            }
        }
        return s;
    };

    std::vector<std::wstring> classes;
    if (policy.useLower) {
        std::wstring s = filterSet(kLower);
        if (!s.empty()) classes.push_back(s);
    }
    if (policy.useUpper) {
        std::wstring s = filterSet(kUpper);
        if (!s.empty()) classes.push_back(s);
    }
    if (policy.useDigits) {
        std::wstring s = filterSet(kDigits);
        if (!s.empty()) classes.push_back(s);
    }
    if (policy.useSymbols) {
        std::wstring s = filterSet(kSymbols);
        if (!s.empty()) classes.push_back(s);
    }

    if (classes.empty()) {
        result.ok = false;
        result.message = L"Select at least one character set, and ensure exclusions leave some characters.";
        return result;
    }

    std::wstring pool;
    for (const auto& c : classes) {
        pool += c;
    }

    std::unordered_set<wchar_t> uniqueChars(pool.begin(), pool.end());
    result.poolSize = static_cast<int>(uniqueChars.size());

    std::wstring output;
    output.reserve(static_cast<size_t>(len));

    // Guarantee at least one character from each enabled class
    const size_t required = std::min(classes.size(), static_cast<size_t>(len));
    for (size_t i = 0; i < required; ++i) {
        const uint32_t idx = SecureRandomInt(static_cast<uint32_t>(classes[i].size()));
        output.push_back(classes[i][idx]);
    }

    // Fill the remainder from the combined pool
    for (size_t i = required; i < static_cast<size_t>(len); ++i) {
        const uint32_t idx = SecureRandomInt(static_cast<uint32_t>(pool.size()));
        output.push_back(pool[idx]);
    }

    // Fisher-Yates shuffle with CSPRNG
    for (size_t i = output.size() - 1; i > 0; --i) {
        const uint32_t swapIdx = SecureRandomInt(static_cast<uint32_t>(i + 1));
        std::swap(output[i], output[swapIdx]);
    }

    result.ok = true;
    result.value = output;
    result.entropyBits = EstimateEntropy(output, result.poolSize);
    result.strengthLabel = StrengthRating(result.entropyBits);
    result.message = L"Password generated locally.";
    return result;
}

PasswordResult PasswordService::GeneratePassphrase(int words, wchar_t separator) {
    PasswordResult result;
    words = std::min(32, std::max(2, words));

    std::wstring passphrase;
    for (int i = 0; i < words; ++i) {
        if (i > 0 && separator != L'\0') {
            passphrase.push_back(separator);
        }
        const uint32_t idx = SecureRandomInt(static_cast<uint32_t>(kWordlistSize));
        passphrase += kWordlist[idx];
    }

    result.ok = true;
    result.value = passphrase;
    // Diceware entropy: log2(wordlistSize) * words
    result.entropyBits = static_cast<double>(words) * std::log2(static_cast<double>(kWordlistSize));
    result.strengthLabel = StrengthRating(result.entropyBits);
    result.poolSize = static_cast<int>(kWordlistSize);
    result.message = L"Passphrase generated locally.";
    return result;
}

HashResult PasswordService::ComputeHash(const std::wstring& password, const HashParameters& params) {
    HashResult result;
    if (password.empty()) {
        result.ok = false;
        result.errorMessage = L"Please enter a password first.";
        return result;
    }

    const std::string pwdUtf8 = WideToUtf8(password);
    const std::string secretUtf8 = WideToUtf8(params.secretKey);

    if (params.algorithm == Algorithm::Sha256Secret || params.algorithm == Algorithm::Pbkdf2Secret) {
        if (params.secretKey.empty()) {
            result.ok = false;
            result.errorMessage = L"Please enter a secret key for this algorithm.";
            return result;
        }
    }

    std::string hashStr;
    std::wstring algInfo;

    switch (params.algorithm) {
        case Algorithm::JoomlaBcrypt: {
            const int cost = std::min(31, std::max(4, params.bcryptCost));
            hashStr = BcryptHash(pwdUtf8, cost);
            algInfo = L"BCrypt (cost " + std::to_wstring(cost) + L")";
            if (pwdUtf8.size() > 72) {
                result.warning = L"Hash generated, but BCrypt only uses the first 72 bytes of this password. Use Argon2id to hash the full length.";
            }
            break;
        }
        case Algorithm::CustomBcrypt: {
            const int cost = std::min(31, std::max(4, params.bcryptCost));
            hashStr = BcryptHash(pwdUtf8, cost);
            algInfo = L"BCrypt (cost " + std::to_wstring(cost) + L")";
            if (pwdUtf8.size() > 72) {
                result.warning = L"Hash generated, but BCrypt only uses the first 72 bytes of this password. Use Argon2id to hash the full length.";
            }
            break;
        }
        case Algorithm::Argon2id: {
            hashStr = Argon2idHash(pwdUtf8, params.argon2);
            algInfo = L"Argon2id (t=" + std::to_wstring(params.argon2.t) +
                      L", m=" + std::to_wstring(params.argon2.m) +
                      L" KiB, p=" + std::to_wstring(params.argon2.p) +
                      L", " + std::to_wstring(params.argon2.hashLength) + L" bytes)";
            break;
        }
        case Algorithm::Sha256: {
            hashStr = HashSha256(pwdUtf8);
            algInfo = L"SHA-256";
            break;
        }
        case Algorithm::Sha512: {
            hashStr = HashSha512(pwdUtf8);
            algInfo = L"SHA-512";
            break;
        }
        case Algorithm::Md5: {
            hashStr = HashMd5(pwdUtf8);
            algInfo = L"MD5";
            break;
        }
        case Algorithm::Sha1: {
            hashStr = HashSha1(pwdUtf8);
            algInfo = L"SHA-1";
            break;
        }
        case Algorithm::Sha256Secret: {
            hashStr = HashSha256Secret(pwdUtf8, secretUtf8);
            algInfo = L"SHA-256 with Secret Key";
            break;
        }
        case Algorithm::Pbkdf2Secret: {
            const int iters = std::max(1, params.pbkdf2Iterations);
            const int klen = std::max(1, params.pbkdf2KeyLength);
            hashStr = HashPbkdf2Secret(pwdUtf8, secretUtf8, iters, klen);
            algInfo = L"PBKDF2-SHA256 (" + std::to_wstring(iters) + L" iterations, " +
                      std::to_wstring(klen) + L" bytes)";
            break;
        }
    }

    if (hashStr.empty()) {
        result.ok = false;
        result.errorMessage = L"Failed to generate hash.";
        return result;
    }

    result.ok = true;
    result.hash = Utf8ToWide(hashStr);
    result.algorithmInfo = algInfo;
    result.hashLength = static_cast<int>(result.hash.size());
    return result;
}

VerifyResult PasswordService::Verify(const std::wstring& password, const std::wstring& hash) {
    VerifyResult result;
    result.attempted = true;

    if (password.empty() || hash.empty()) {
        result.match = false;
        result.message = L"Enter both the hash and the password.";
        return result;
    }

    const std::string pwdUtf8 = WideToUtf8(password);
    const std::string hashUtf8 = WideToUtf8(hash);

    if (hashUtf8.compare(0, 3, "$2a") == 0 || hashUtf8.compare(0, 3, "$2b") == 0 ||
        hashUtf8.compare(0, 3, "$2y") == 0) {
        result.match = BcryptVerify(pwdUtf8, hashUtf8);
        result.message = result.match ? L"Match: this password matches the hash."
                                      : L"No match: this password does not match the hash.";
        return result;
    }

    if (hashUtf8.compare(0, 10, "$argon2id$") == 0) {
        result.match = Argon2idVerify(pwdUtf8, hashUtf8);
        result.message = result.match ? L"Match: this password matches the hash."
                                      : L"No match: this password does not match the hash.";
        return result;
    }

    result.match = false;
    result.message = L"Unsupported hash format. Verify supports BCrypt ($2y$…) and Argon2id ($argon2id$…) hashes.";
    return result;
}

}  // namespace ArgpiSuite::Password
