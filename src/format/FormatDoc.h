// ArgpiSuite native formatter — Prettier-compatible Doc IR.
//
// Docs are stored in an arena and referenced by 32-bit handles. This keeps the
// tree compact, makes lifetime trivial (one arena per format run) and avoids
// raw new/delete. The builder API mirrors Prettier's doc-builders.js.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Format {

using DocId = std::int32_t;
inline constexpr DocId kNoDoc = -1;

enum class DocKind : std::uint8_t {
    Text,
    Concat,
    Group,
    Indent,
    Align,
    Dedent,
    Line,
    SoftLine,
    HardLine,
    LiteralLine,
    IfBreak,
    Fill,
    LineSuffix,
    LineSuffixBoundary,
    BreakParent,
    Trim,
    Cursor,
};

struct DocNode {
    DocKind kind = DocKind::Text;
    bool shouldBreak = false;
    int groupId = 0;
    int alignWidth = 0;
    DocId contents = kNoDoc;
    DocId breakContents = kNoDoc;
    DocId flatContents = kNoDoc;
    std::string text;
    std::vector<DocId> parts;            // Concat / Fill
    std::vector<DocId> expandedStates;   // Group (conditionalGroup)
};

class DocArena {
public:
    explicit DocArena(std::size_t reserve = 512);

    DocId Text(std::string_view text);
    DocId Concat(std::vector<DocId> parts);
    DocId Concat(std::initializer_list<DocId> parts);

    /// Group `contents`. `shouldBreak` forces MODE_BREAK.
    DocId Group(DocId contents, bool shouldBreak = false, int groupId = 0);
    /// conditionalGroup: first state is the flat form, last is the fully expanded form.
    DocId ConditionalGroup(std::vector<DocId> states, bool shouldBreak = false, int groupId = 0);

    DocId Indent(DocId contents);
    DocId Align(int width, DocId contents);
    DocId Dedent(DocId contents);

    DocId Line();
    DocId SoftLine();
    DocId HardLine();
    DocId LiteralLine();

    /// `groupId` is the DocId of the governing Group (kNoDoc = current mode).
    DocId IfBreak(DocId breakContents, DocId flatContents = kNoDoc, int groupId = kNoDoc);
    DocId Fill(std::vector<DocId> parts);
    DocId LineSuffix(DocId contents);
    DocId LineSuffixBoundary();
    DocId BreakParent();
    DocId Trim();
    DocId Cursor();

    /// Reserves a handle so a group can reference itself (ifBreak groupId).
    DocId Reserve();
    void Set(DocId id, DocNode node);

    [[nodiscard]] const DocNode& Get(DocId id) const;
    [[nodiscard]] std::size_t Size() const noexcept { return nodes_.size(); }

    static bool IsLine(DocKind kind) noexcept {
        return kind == DocKind::Line || kind == DocKind::SoftLine ||
               kind == DocKind::HardLine || kind == DocKind::LiteralLine;
    }

private:
    DocId Add(DocNode node);

    std::vector<DocNode> nodes_;
};

}  // namespace ArgpiSuite::Format
