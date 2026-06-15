/**
 * @file CommandParser.cpp
 * @brief Implements the CommandParser — the heart of the NexusDB CLI.
 */

#include "CommandParser.h"
#include "NexusManager.h"
#include "GridFormatter.h"
#include "QueryEngine.h"
#include "ANSIColors.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <map>
#include <future> // ADDED FOR MULTI-THREADING / CONCURRENCY

namespace nexusdb {

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

std::string CommandParser::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string CommandParser::toUpper(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    return u;
}

std::vector<std::string> CommandParser::tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

void CommandParser::printError(const std::string& msg) {
    std::cout << color::BOLD_RED << "  [ERROR] " << color::RESET
              << color::RED << msg << color::RESET << "\n";
}

void CommandParser::printSuccess(const std::string& msg) {
    std::cout << color::BOLD_GREEN << "  [OK] " << color::RESET
              << color::GREEN << msg << color::RESET << "\n";
}

void CommandParser::printInfo(const std::string& msg) {
    std::cout << color::DIM_WHITE << "  " << msg << color::RESET << "\n";
}

// ═══════════════════════════════════════════════════════════════
// Main dispatcher
// ═══════════════════════════════════════════════════════════════

bool CommandParser::execute(const std::string& input) {
    std::string trimmed = trim(input);
    if (trimmed.empty()) return true;

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) return true;

    std::string cmd = toUpper(tokens[0]);
    auto& mgr = NexusManager::getInstance();
    bool inTable = (mgr.getCurrentTable() != nullptr);

    // ── Global commands ──────────────────────────────────────
    if (cmd == "EXIT" || cmd == "QUIT") return false;

    if (cmd == "CLEAR") {
        #ifdef _WIN32
        std::system("cls");
        #else
        std::system("clear");
        #endif
        return true;
    }

    // ── Table context commands ───────────────────────────────
    if (inTable) {
        if (cmd == "BACK") {
            mgr.clearCurrentTable();
            return true;
        }
        if (cmd == "HELP")     { cmdHelpTable(); return true; }
        if (cmd == "DESCRIBE" || cmd == "DESC") { cmdDescribe(); return true; }

        if (cmd == "INSERT") { cmdInsertInto(tokens); return true; }
        if (cmd == "SELECT") { cmdSelect(tokens, trimmed); return true; }
        if (cmd == "UPDATE") { cmdUpdate(tokens, trimmed); return true; }
        if (cmd == "DELETE") { cmdDelete(tokens, trimmed); return true; }

        // Allow table-level CREATE/DROP/SHOW/USE too
        if (cmd == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLES") {
            cmdShowTables(); return true;
        }
        if (cmd == "USE")   { cmdUseTable(tokens); return true; }
        if (cmd == "CREATE" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLE") {
            cmdCreateTable(tokens); return true;
        }
        if (cmd == "DROP" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLE") {
            cmdDropTable(tokens); return true;
        }

        printError("Unknown command: '" + tokens[0] + "'. Type HELP for available commands.");
        return true;
    }

    // ── Root context commands ────────────────────────────────
    if (cmd == "HELP")  { cmdHelpRoot(); return true; }
    if (cmd == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLES") {
        cmdShowTables(); return true;
    }
    if (cmd == "USE")   { cmdUseTable(tokens); return true; }
    if (cmd == "CREATE" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLE") {
        cmdCreateTable(tokens); return true;
    }
    if (cmd == "DROP" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLE") {
        cmdDropTable(tokens); return true;
    }

    printError("Unknown command: '" + tokens[0] + "'. Type HELP for available commands.");
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Root commands
// ═══════════════════════════════════════════════════════════════

void CommandParser::cmdShowTables() {
    auto names = NexusManager::getInstance().showTables();
    if (names.empty()) {
        printInfo("No tables found. Use CREATE TABLE to get started.");
        return;
    }
    std::cout << "\n" << color::BOLD_CYAN << "  Tables" << color::RESET << "\n";
    std::cout << color::DIM_WHITE << "  ────────────────────────" << color::RESET << "\n";
    for (const auto& n : names) {
        std::cout << "   " << color::BOLD_WHITE << "\xe2\x96\xb8 " << n
                  << color::RESET << "\n";
    }
    std::cout << "\n" << color::DIM_WHITE << "  " << names.size()
              << " table(s) found." << color::RESET << "\n";
}

void CommandParser::cmdCreateTable(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4) {
        printError("Syntax: CREATE TABLE <name> <col1:type, col2:type, ...>");
        return;
    }

    std::string tableName = tokens[2];

    std::string colDefs;
    for (size_t i = 3; i < tokens.size(); ++i) {
        if (i > 3) colDefs += " ";
        colDefs += tokens[i];
    }

    if (!colDefs.empty() && colDefs.front() == '[') colDefs.erase(0, 1);
    if (!colDefs.empty() && colDefs.back() == ']')  colDefs.pop_back();

    std::vector<Column> schema;
    std::istringstream ss(colDefs);
    std::string token;
    bool first = true;
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (token.empty()) continue;
        try {
            Column col = Column::deserialize(token);
            if (first) { col.isPrimaryKey = true; first = false; }
            schema.push_back(col);
        } catch (const std::exception& e) {
            printError(std::string("Bad column definition '") + token + "': " + e.what());
            return;
        }
    }

    std::string errMsg;
    if (NexusManager::getInstance().createTable(tableName, schema, errMsg)) {
        printSuccess("Table '" + tableName + "' created successfully.");
    } else {
        printError(errMsg);
    }
}

void CommandParser::cmdDropTable(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        printError("Syntax: DROP TABLE <name>");
        return;
    }
    std::string name = tokens[2];

    std::cout << color::BOLD_YELLOW
              << "  Are you sure you want to drop table '" << name
              << "'? This will delete the data file. (yes/no): "
              << color::RESET;
    std::string confirm;
    std::getline(std::cin, confirm);
    confirm = trim(confirm);
    std::transform(confirm.begin(), confirm.end(), confirm.begin(), ::tolower);

    if (confirm != "yes" && confirm != "y") {
        printInfo("Drop cancelled.");
        return;
    }

    std::string errMsg;
    if (NexusManager::getInstance().dropTable(name, errMsg)) {
        printSuccess("Table '" + name + "' dropped.");
    } else {
        printError(errMsg);
    }
}

void CommandParser::cmdUseTable(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        printError("Syntax: USE <tablename>");
        return;
    }
    std::string name = tokens[1];
    if (NexusManager::getInstance().useTable(name)) {
        printSuccess("Switched to table '" + NexusManager::getInstance().getCurrentTable()->getName() + "'.");
    } else {
        printError("Table '" + name + "' not found. Use SHOW TABLES to list available tables.");
    }
}

void CommandParser::cmdHelpRoot() {
    std::cout << "\n" << color::BOLD_CYAN << "  NexusDB Commands (Root Context)"
              << color::RESET << "\n";
    std::cout << color::DIM_WHITE << "  ══════════════════════════════════════════════"
              << color::RESET << "\n";
    auto row = [](const std::string& cmd, const std::string& desc) {
        std::cout << "  " << color::BOLD_YELLOW << std::left;
        std::string padded = cmd;
        while (padded.size() < 38) padded += ' ';
        std::cout << padded << color::RESET << color::DIM_WHITE << desc
                  << color::RESET << "\n";
    };
    row("SHOW TABLES",                          "List all tables");
    row("CREATE TABLE <n> <c1:type, c2:type>",  "Create a new table");
    row("USE <tablename>",                      "Enter a table's context");
    row("DROP TABLE <name>",                    "Delete a table and its file");
    row("CLEAR",                                "Clear the screen");
    row("HELP",                                 "Show this help message");
    row("EXIT",                                 "Quit NexusDB");
    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════════════
// Table-context commands
// ═══════════════════════════════════════════════════════════════

void CommandParser::cmdInsertInto(const std::vector<std::string>& /*tokens*/) {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    if (!tbl) { printError("No table selected."); return; }

    std::cout << color::BOLD_CYAN << "\n  Insert into '" << tbl->getName()
              << "' — enter values for each column:\n" << color::RESET;

    Row row;
    for (const auto& col : tbl->getColumns()) {
        std::string prompt = std::string("  ") + color::BOLD_WHITE + col.name + color::RESET +
            " (" + columnTypeToString(col.type) +
            (col.isPrimaryKey ? ", PK" : "") + "): ";
        std::cout << prompt;

        std::string value;
        std::getline(std::cin, value);
        value = trim(value);

        if (value.empty()) {
            printError("Value cannot be empty. Insert aborted.");
            return;
        }
        if (!Table::validateType(value, col.type)) {
            printError("Invalid " + columnTypeToString(col.type) +
                       " value for '" + col.name + "'. Insert aborted.");
            return;
        }
        row.set(col.name, value);
    }

    std::string errMsg;
    if (tbl->insertRow(row, errMsg)) {
        printSuccess("Row inserted successfully.");
    } else {
        printError(errMsg);
    }
}

void CommandParser::cmdSelect(const std::vector<std::string>& /*tokens*/,
                              const std::string& rawInput) {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    if (!tbl) { printError("No table selected."); return; }

    std::cout << "\n";

    std::string upper = toUpper(rawInput);
    size_t wherePos = upper.find(" WHERE ");

    if (wherePos != std::string::npos) {
        std::string whereStr = rawInput.substr(wherePos + 7);
        WhereClause clause = QueryEngine::parseWhere(whereStr);

        if (!clause.valid) {
            printError("WHERE parse error: " + clause.error);
            return;
        }

        auto allRows = tbl->selectAll();

        // --- MULTI-THREADING / CONCURRENCY ADDED HERE ---
        // Executes the data filtering on a background thread
        std::future<std::vector<Row>> futureFiltered = std::async(std::launch::async, [&]() {
            return QueryEngine::filter(allRows, tbl->getColumns(), clause);
        });

        std::cout << color::DIM_WHITE << "  [Executing concurrent search on background thread...]\n" << color::RESET;

        // Thread syncs back here when the query is finished processing
        auto filtered = futureFiltered.get(); 
        GridFormatter::render(tbl->getColumns(), filtered);
    } else {
        auto rows = tbl->selectAll();
        GridFormatter::render(tbl->getColumns(), rows);
    }
}

void CommandParser::cmdUpdate(const std::vector<std::string>& /*tokens*/,
                              const std::string& rawInput) {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    if (!tbl) { printError("No table selected."); return; }

    const Column* pk = tbl->getPrimaryKey();
    if (!pk) { printError("Table has no primary key — cannot UPDATE."); return; }

    std::string upper = toUpper(rawInput);
    size_t setPos   = upper.find(" SET ");
    size_t wherePos = upper.find(" WHERE ");

    if (setPos == std::string::npos || wherePos == std::string::npos || wherePos <= setPos) {
        printError("Syntax: UPDATE <table> SET col=val,... WHERE " + pk->name + " = <value>");
        return;
    }

    std::string setPart = rawInput.substr(setPos + 5, wherePos - setPos - 5);
    std::string wherePart = rawInput.substr(wherePos + 7);

    size_t eqPos = wherePart.find('=');
    if (eqPos == std::string::npos) {
        printError("WHERE clause must be: " + pk->name + " = <value>");
        return;
    }
    std::string whereVal = trim(wherePart.substr(eqPos + 1));
    if (whereVal.size() >= 2 &&
        ((whereVal.front() == '\'' && whereVal.back() == '\'') ||
         (whereVal.front() == '"'  && whereVal.back() == '"'))) {
        whereVal = whereVal.substr(1, whereVal.size() - 2);
    }

    std::map<std::string, std::string> updates;
    std::istringstream setStream(setPart);
    std::string assignment;
    while (std::getline(setStream, assignment, ',')) {
        assignment = trim(assignment);
        size_t eq = assignment.find('=');
        if (eq == std::string::npos) {
            printError("Invalid SET syntax near '" + assignment + "'.");
            return;
        }
        std::string col = trim(assignment.substr(0, eq));
        std::string val = trim(assignment.substr(eq + 1));
        if (val.size() >= 2 &&
            ((val.front() == '\'' && val.back() == '\'') ||
             (val.front() == '"'  && val.back() == '"'))) {
            val = val.substr(1, val.size() - 2);
        }
        updates[col] = val;
    }

    std::string errMsg;
    if (tbl->updateRow(whereVal, updates, errMsg)) {
        printSuccess("Row updated successfully.");
    } else {
        printError(errMsg);
    }
}

void CommandParser::cmdDelete(const std::vector<std::string>& /*tokens*/,
                              const std::string& rawInput) {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    if (!tbl) { printError("No table selected."); return; }

    const Column* pk = tbl->getPrimaryKey();
    if (!pk) { printError("Table has no primary key — cannot DELETE."); return; }

    std::string upper = toUpper(rawInput);
    size_t wherePos = upper.find(" WHERE ");
    if (wherePos == std::string::npos) {
        printError("Syntax: DELETE FROM <table> WHERE " + pk->name + " = <value>");
        return;
    }

    std::string wherePart = rawInput.substr(wherePos + 7);
    size_t eqPos = wherePart.find('=');
    if (eqPos == std::string::npos) {
        printError("WHERE clause must be: " + pk->name + " = <value>");
        return;
    }
    std::string whereVal = trim(wherePart.substr(eqPos + 1));
    if (whereVal.size() >= 2 &&
        ((whereVal.front() == '\'' && whereVal.back() == '\'') ||
         (whereVal.front() == '"'  && whereVal.back() == '"'))) {
        whereVal = whereVal.substr(1, whereVal.size() - 2);
    }

    std::string errMsg;
    if (tbl->deleteRow(whereVal, errMsg)) {
        printSuccess("Row deleted successfully.");
    } else {
        printError(errMsg);
    }
}

void CommandParser::cmdDescribe() {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    if (!tbl) { printError("No table selected."); return; }

    std::cout << "\n" << color::BOLD_CYAN << "  Schema for '" << tbl->getName()
              << "'" << color::RESET << "\n";
    std::cout << color::DIM_WHITE << "  ────────────────────────────────────"
              << color::RESET << "\n";
    for (const auto& col : tbl->getColumns()) {
        std::cout << "   " << color::BOLD_WHITE << col.name << color::RESET
                  << " : " << color::CYAN << columnTypeToString(col.type)
                  << color::RESET;
        if (col.isPrimaryKey) {
            std::cout << " " << color::BOLD_YELLOW << "[PK]" << color::RESET;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

void CommandParser::cmdHelpTable() {
    Table* tbl = NexusManager::getInstance().getCurrentTable();
    std::string tName = tbl ? tbl->getName() : "table";

    std::cout << "\n" << color::BOLD_CYAN << "  NexusDB Commands (Table: "
              << tName << ")" << color::RESET << "\n";
    std::cout << color::DIM_WHITE << "  ══════════════════════════════════════════════"
              << color::RESET << "\n";
    auto row = [](const std::string& cmd, const std::string& desc) {
        std::cout << "  " << color::BOLD_YELLOW << std::left;
        std::string padded = cmd;
        while (padded.size() < 44) padded += ' ';
        std::cout << padded << color::RESET << color::DIM_WHITE << desc
                  << color::RESET << "\n";
    };
    row("INSERT INTO " + tName,                      "Step-by-step insert wizard");
    row("SELECT * FROM " + tName,                    "Show all rows");
    row("SELECT ... WHERE col = val",                 "Filter with = operator");
    row("SELECT ... WHERE col > val",                 "Operators: = != > < >= <=");
    row("SELECT ... WHERE a > 1 AND b = x",           "Combine with AND / OR");
    row("UPDATE " + tName + " SET c=v WHERE pk=v",   "Update row by PK");
    row("DELETE FROM " + tName + " WHERE pk=v",      "Delete row by PK");
    row("DESCRIBE",                                   "Show table schema");
    row("BACK",                                       "Return to root context");
    row("HELP",                                       "Show this help message");
    std::cout << "\n";
}

} // namespace nexusdb
