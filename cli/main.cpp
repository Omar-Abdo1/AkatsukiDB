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

        std::string input;
        while (true) {
            std::cout << COLOR_DARK_RED << "akatsuki> " << COLOR_RESET;

            if (!std::getline(std::cin, input)) break; // Handle EOF (Ctrl+D)

            Trim(input);

            if (input.empty()) continue;
            if (EqualsIgnoreCase(input, "exit")) break;

            if (EqualsIgnoreCase(input, "clear") || EqualsIgnoreCase(input, "cls")) {
                // ANSI escape code to clear the terminal screen and move cursor to top-left
                std::cout << "\033[2J\033[1;1H";
                continue;
            }

            QueryResult result = engine->Execute(input);
            PrintResult(result);
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
                        // Assuming your DBObject has a .ToString() or is already a string
                        auto &dbObject = it->second;
                        cells.push_back( DbObjectToString(dbObject) );
                    } else {
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