#include <filesystem>
#include <iostream>
#include <memory>

#include "AkatsukiDB/AQL/Tokenizer.hpp"
#include "AkatsukiDB/Engine/AkatsukiEngine.hpp"
#include "AkatsukiDB/Engine/QueryResult.hpp"
#include "AkatsukiDB/Index/BPlusTree.hpp"
#include "AkatsukiDB/Index/BPlusTreeNode.hpp"
#include "AkatsukiDB/Index/IndexKey.hpp"
#include "AkatsukiDB/Storage/Page.hpp"
#include "AkatsukiDB/Storage/PageManager.hpp"
#include "AkatsukiDB/Storage/StorageLayout.hpp"
#include "AkatsukiDB/Table/TableManager.hpp"
#include "AkatsukiDB/Table/TableRegistry.hpp"
using namespace std;

#include <nlohmann/json.hpp>
#include <replxx.hxx>
#include <chrono>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <iomanip>


namespace AkatsukiDB::Console {

    // --- ANSI Color Codes for Linux Terminal ---
    const std::string COLOR_RED = "\033[31m";
    const std::string COLOR_DARK_RED = "\033[38;5;88m";
    const std::string COLOR_GREEN = "\033[32m";
    const std::string COLOR_DARK_GRAY = "\033[90m";
    const std::string COLOR_RESET = "\033[0m";

    // --- Helper: Trim whitespace from a string ---
    void Trim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    }

    // --- Helper: Case-insensitive string comparison ---
    bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    }

    // --- Forward Declarations ---
    std::vector<int> CalculateWidth(const QueryResult& result);
    void PrintSeparator(const std::vector<int>& widths);
    void PrintRow(const std::vector<std::string>& cells, const std::vector<int>& widths);
    void PrintResult(const QueryResult& result);

    void Run() {
        // Instantiate the engine (RAII will handle disposal/closing)
        unique_ptr<AkatsukiEngine> engine=make_unique<AkatsukiEngine>("./akatsuki_data");

        std::cout << COLOR_RED;
        // Using C++ Raw String Literals R"(...)" so we don't have to escape every backslash
        std::cout << R"(  ___  _  __   _ ____  _   _ _  _____   ____  ____
 / _ \| |/ /  / |___ \| | | | |/ /_ _| |  _ \| __ )
| |_| | ' /   | | __) | | | | ' / | |  | | | |  _ \
|  _  | . \   | |/ __/| |_| | . \ | |  | |_| | |_) |
|_| |_|_|\_\  |_|_____||\___/|_|\_\___| |____/|____/)" << "\n";

        std::cout << COLOR_RESET;
        std::cout << "\nAkatsukiDB v1.0  |  type EXIT to quit\n\n";

        replxx::Replxx rx;
    
  rx.set_highlighter_callback(
    [&](std::string const& input, replxx::Replxx::colors_t& colors) {
        const auto& keywords = Tokenizer::GetKeywords();

        for (size_t i = 0; i < input.size(); ++i)
            colors[i] = replxx::Replxx::Color::DEFAULT;

        size_t pos = 0;
        while (pos < input.size()) {
            // skip whitespace
            while (pos < input.size() && std::isspace((unsigned char)input[pos]))
                ++pos;
            if (pos >= input.size()) break;

            size_t start = pos;
            char c = input[pos];

            // string literal
            if (c == '"' || c == '\'') {
                char quote = c;
                ++pos;
                while (pos < input.size() && input[pos] != quote) ++pos;
                if (pos < input.size()) ++pos;
                for (size_t i = start; i < pos; ++i)
                    colors[i] = replxx::Replxx::Color::GREEN;
                continue;
            }

            // number
            if (std::isdigit((unsigned char)c)) {
                while (pos < input.size()
                    && (std::isdigit((unsigned char)input[pos])
                        || input[pos] == '.'))
                    ++pos;
                for (size_t i = start; i < pos; ++i)
                    colors[i] = replxx::Replxx::Color::CYAN;
                continue;
            }

            // keyword or identifier
            if (std::isalpha((unsigned char)c) || c == '_') {
                while (pos < input.size()
                    && (std::isalnum((unsigned char)input[pos])
                        || input[pos] == '_'))
                    ++pos;
                std::string word = input.substr(start, pos - start);
                for (auto& ch : word) ch = std::tolower(ch);
                if (keywords.count(word))
                    for (size_t i = start; i < pos; ++i)
                        colors[i] = replxx::Replxx::Color::MAGENTA;
                continue;
            }

            ++pos;
        }
    }
);
  

rx.history_load(".akatsuki_history");

while (true) {

    char const* cinput = rx.input("akatsuki> ");

    if (!cinput)
        break; // Ctrl+D

    std::string input(cinput);

    Trim(input);

    if (input.empty())
        continue;

    rx.history_add(input);
    rx.history_save(".akatsuki_history");

    if (EqualsIgnoreCase(input, "exit"))
        break;

    if (EqualsIgnoreCase(input, "clear") ||
        EqualsIgnoreCase(input, "cls")) {
        std::cout << "\033[2J\033[1;1H";
        continue;
    }
    auto start = std::chrono::steady_clock::now();
    QueryResult result = engine->Execute(input);
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    PrintResult(result);
    if (!result.IsError) {
        std::cout << COLOR_DARK_GRAY
              << "  Time: " << std::fixed << std::setprecision(3)
              << ms << " ms"
              << COLOR_RESET << "\n";
    }
}





        std::cout << "Goodbye.\n";
    }

    // ------------------------------------------------------------------------
    // Formatting and Output Logic
    // ------------------------------------------------------------------------

    void PrintResult(const QueryResult& result) {
        // Error
        if (result.IsError) {
            std::cout << COLOR_RED << "ERROR: " << result.ErrorMessage.value() << COLOR_RESET << "\n";
            return;
        }

        // Select (Returning Rows)
        if (!result.Rows.empty() || !result.Columns.empty()) {
            std::vector<int> widths = CalculateWidth(result);

            // Header
            PrintSeparator(widths);
            PrintRow(result.Columns, widths);
            PrintSeparator(widths);

            // Rows
            for (const auto& row : result.Rows) {
                std::vector<std::string> cells;
                for (const std::string& col : result.Columns) {
                    // Check if column exists in the row map
                    auto it = row.find(col);
                    if (it != row.end()) {
                        auto &dbObject = it->second;
                        if (std::holds_alternative<std::string>(dbObject)
                               && std::get<std::string>(dbObject).empty())
                            cells.push_back("NULL");
                        else
                            cells.push_back(DbObjectToString(dbObject));
                    }
                    else {
                        cells.push_back("NULL");
                    }
                }
                PrintRow(cells, widths);
            }

            PrintSeparator(widths);

            std::cout << COLOR_DARK_GRAY << "  " << result.Rows.size() << " row(s) returned";
            if (result.PlanUsed.has_value()) {
                std::cout << "  |  plan: " << result.PlanUsed.value();
            }
            std::cout << COLOR_RESET << "\n";
            return;
        }

        // Rows Affected (Insert/Update/Delete/Create)
        std::cout << COLOR_GREEN << "OK  ";
        if (result.PlanUsed.has_value()) {
            std::cout << result.PlanUsed.value();
        } else {
            std::cout << result.RowsAffected << " row(s) affected";
        }
        std::cout << COLOR_RESET << "\n";
    }

    std::vector<int> CalculateWidth(const QueryResult& result) {
        std::vector<int> widths;
        widths.reserve(result.Columns.size());

        for (const std::string& column : result.Columns) {
            int maxWidth = static_cast<int>(column.length());

            for (const auto& row : result.Rows) {
                auto it = row.find(column);
                std::string value = (it != row.end()) ? DbObjectToString(it->second) : "NULL";
                if (static_cast<int>(value.length()) > maxWidth) {
                    maxWidth = static_cast<int>(value.length());
                }
            }
            widths.push_back(maxWidth);
        }
        return widths;
    }

    void PrintSeparator(const std::vector<int>& widths) {
        std::cout << "+";
        for (int w : widths) {
            // Creates a string of '-' of length (w + 2)
            std::cout << std::string(w + 2, '-') << "+";
        }
        std::cout << "\n";
    }

    void PrintRow(const std::vector<std::string>& cells, const std::vector<int>& widths) {
        std::cout << "| ";
        for (size_t i = 0; i < cells.size(); ++i) {
            // std::left and std::setw handle the "PadRight" logic from C#
            std::cout << std::left << std::setw(widths[i]) << cells[i];

            if (i != cells.size() - 1) {
                std::cout << " | ";
            }
        }
        std::cout << " |\n";
    }

}

int main() {

    AkatsukiDB::Console::Run();
}