#pragma once

#include <string>

namespace dm
{
// Turns an imported file name into a readable pad name:
//   "kick_heavy-01"  ->  "Kick heavy 01"
// Separators become spaces, spaces are collapsed/trimmed, first letter is capitalised.
inline std::string cleanSampleName (const std::string& raw)
{
    std::string out;
    bool lastSpace = true;   // also trims leading separators
    for (char ch : raw)
    {
        const bool separator = (ch == '_' || ch == '-' || ch == ' ' || ch == '\t');
        if (separator)
        {
            if (! lastSpace) out.push_back (' ');
            lastSpace = true;
        }
        else
        {
            out.push_back (ch);
            lastSpace = false;
        }
    }
    while (! out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) return "Sample";
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char> (out[0] - 'a' + 'A');
    return out;
}
} // namespace dm
