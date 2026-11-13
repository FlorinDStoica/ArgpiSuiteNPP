#include "FormatDoc.h"

#include <utility>

namespace ArgpiSuite::Format {

DocArena::DocArena(std::size_t reserve) { nodes_.reserve(reserve); }

DocId DocArena::Add(DocNode node) {
    nodes_.push_back(std::move(node));
    return static_cast<DocId>(nodes_.size() - 1);
}

DocId DocArena::Text(std::string_view text) {
    DocNode node;
    node.kind = DocKind::Text;
    node.text.assign(text.data(), text.size());
    return Add(std::move(node));
}

DocId DocArena::Concat(std::vector<DocId> parts) {
    if (parts.empty()) {
        return Text({});
    }
    if (parts.size() == 1) {
        return parts.front();
    }
    DocNode node;
    node.kind = DocKind::Concat;
    node.parts = std::move(parts);
    return Add(std::move(node));
}

DocId DocArena::Concat(std::initializer_list<DocId> parts) {
    return Concat(std::vector<DocId>(parts));
}

DocId DocArena::Group(DocId contents, bool shouldBreak, int groupId) {
    DocNode node;
    node.kind = DocKind::Group;
    node.contents = contents;
    node.shouldBreak = shouldBreak;
    node.groupId = groupId;
    return Add(std::move(node));
}

DocId DocArena::ConditionalGroup(std::vector<DocId> states, bool shouldBreak, int groupId) {
    if (states.empty()) {
        return Text({});
    }
    if (states.size() == 1) {
        return Group(states.front(), shouldBreak, groupId);
    }
    DocNode node;
    node.kind = DocKind::Group;
    node.contents = states.front();
    node.expandedStates = std::move(states);
    node.shouldBreak = shouldBreak;
    node.groupId = groupId;
    return Add(std::move(node));
}

DocId DocArena::Indent(DocId contents) {
    DocNode node;
    node.kind = DocKind::Indent;
    node.contents = contents;
    return Add(std::move(node));
}

DocId DocArena::Align(int width, DocId contents) {
    DocNode node;
    node.kind = DocKind::Align;
    node.alignWidth = width;
    node.contents = contents;
    return Add(std::move(node));
}

DocId DocArena::Dedent(DocId contents) {
    DocNode node;
    node.kind = DocKind::Dedent;
    node.contents = contents;
    return Add(std::move(node));
}

DocId DocArena::Line() {
    DocNode node;
    node.kind = DocKind::Line;
    return Add(std::move(node));
}

DocId DocArena::SoftLine() {
    DocNode node;
    node.kind = DocKind::SoftLine;
    return Add(std::move(node));
}

DocId DocArena::HardLine() {
    DocNode node;
    node.kind = DocKind::HardLine;
    return Add(std::move(node));
}

DocId DocArena::LiteralLine() {
    DocNode node;
    node.kind = DocKind::LiteralLine;
    return Add(std::move(node));
}

DocId DocArena::IfBreak(DocId breakContents, DocId flatContents, int groupId) {
    DocNode node;
    node.kind = DocKind::IfBreak;
    node.breakContents = breakContents;
    node.flatContents = flatContents;
    node.groupId = groupId;
    return Add(std::move(node));
}

DocId DocArena::Fill(std::vector<DocId> parts) {
    DocNode node;
    node.kind = DocKind::Fill;
    node.parts = std::move(parts);
    return Add(std::move(node));
}

DocId DocArena::LineSuffix(DocId contents) {
    DocNode node;
    node.kind = DocKind::LineSuffix;
    node.contents = contents;
    return Add(std::move(node));
}

DocId DocArena::LineSuffixBoundary() {
    DocNode node;
    node.kind = DocKind::LineSuffixBoundary;
    return Add(std::move(node));
}

DocId DocArena::BreakParent() {
    DocNode node;
    node.kind = DocKind::BreakParent;
    return Add(std::move(node));
}

DocId DocArena::Trim() {
    DocNode node;
    node.kind = DocKind::Trim;
    return Add(std::move(node));
}

DocId DocArena::Cursor() {
    DocNode node;
    node.kind = DocKind::Cursor;
    return Add(std::move(node));
}

DocId DocArena::Reserve() { return Add(DocNode{}); }

void DocArena::Set(DocId id, DocNode node) {
    if (id >= 0 && static_cast<std::size_t>(id) < nodes_.size()) {
        nodes_[static_cast<std::size_t>(id)] = std::move(node);
    }
}

const DocNode& DocArena::Get(DocId id) const {
    static const DocNode kEmpty{};
    if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) {
        return kEmpty;
    }
    return nodes_[static_cast<std::size_t>(id)];
}

}  // namespace ArgpiSuite::Format
