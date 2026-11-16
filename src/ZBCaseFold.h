#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Unicode full case folding (CaseFolding.txt statuses C and F) over UTF-8, for
// comparing an echoed text with a secret without regard to case: "Pässwörd-ßeta"
// is found in "PÄSSWÖRD-SSETA". Bytes that are not well-formed UTF-8 are kept as
// they are.
//
// Returns the folded text. When spans is not null it receives, for every byte
// of the result, the [begin, end) byte range of the original character that
// produced it, so a match in the folded text can be masked in the original.
struct ZbFoldSpan {
    size_t begin;
    size_t end;
};
std::string zbCaseFold(const std::string& text, std::vector<ZbFoldSpan>* spans = nullptr);
