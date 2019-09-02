#define CATCH_CONFIG_MAIN

#include "piecetable.h"
#include <catch2/catch.hpp>

#include <iostream>

void dumpTable(PieceTable& table) {
    std::cerr << "Dumping table: \n";
    for (const auto& piece : table.table()) {
        stdx::string_view contents;
        switch (piece.type) {
            case PieceTable::Piece::kOriginal:
                contents = table.originalFileView().substr(piece.start, piece.length);
                break;
            case PieceTable::Piece::kAddBuffer:
                contents = stdx::string_view(table.addBuffer()).substr(piece.start, piece.length);
                break;
        }
        std::cerr << "Piece type: {} start: {} length {} contents \"{}\"\n"_format(
            static_cast<int>(piece.type), piece.start, piece.length, contents);
    }

    std::cerr << "\n\n";
}

void testContents(PieceTable& table, stdx::string_view match) {
    dumpTable(table);
    std::string str;
    std::copy(table.begin(), table.end(), std::back_inserter(str));
    REQUIRE(str == match);

    str.clear();
    std::copy(table.begin(), table.end(), std::back_inserter(str));
    REQUIRE(str == match);
}

std::string dumpLine(const Line& line) {
    std::string out;
    std::copy(line.begin(), line.end(), std::back_inserter(out));
    REQUIRE(out.size() == line.size());
    return out;
}

struct TempFile {
    TempFile(stdx::string_view contents)
        : path(std::tmpnam(nullptr)), contents(contents.to_string()) {
        std::ofstream file(path);
        file.exceptions(std::ios::badbit);
        file << contents;
    }

    ~TempFile() {
        ::unlink(path.c_str());
    }

    const std::string path;
    const std::string contents;
};

TEST_CASE("AppendBufferOnly", "[PieceTable]") {
    PieceTable table;

    table.insert(table.end(), 'f');
    table.insert(table.end(), 'o');
    table.insert(table.end(), 'o');
    table.insert(table.end(), '\n');
    table.insert(table.end(), ' ');
    table.insert(table.end(), 'b');
    table.insert(table.end(), 'a');
    table.insert(table.end(), 'r');

    testContents(table, "foo\n bar");
    dumpTable(table);
}

TEST_CASE("FileLoad", "[PieceTable]") {
    TempFile testFile("bizz buzz");
    PieceTable table(testFile.path);

    testContents(table, testFile.contents);

    auto it = std::next(table.begin(), 4);
    it = table.insert(++it, 'f');
    it = table.insert(++it, 'o');
    it = table.insert(++it, 'o');
    it = table.insert(++it, ' ');

    testContents(table, "bizz foo buzz"_sv);
}


TEST_CASE("EraseOriginalFile", "[PieceTable]") {
    TempFile testFile("bizz buzz");
    PieceTable table(testFile.path);

    testContents(table, testFile.contents);

    auto it = std::next(table.begin(), 4);
    table.erase(it);

    testContents(table, "bizzbuzz"_sv);

    it = std::next(table.begin(), 6);
    table.erase(it);

    testContents(table, "bizzbuz"_sv);

    table.erase(table.begin());
    testContents(table, "izzbuz"_sv);

    it = table.begin();
    table.insert(it, 'f');

    it = std::next(table.begin(), 7);
    table.insert(it, 'f');
    testContents(table, "fizzbuzf"_sv);

    it = table.begin();
    ++it;
    ++it;
    ++it;
    it = table.erase(it);
    ++it;
    table.insert(it, 'u');
    testContents(table, "fizbuuzf"_sv);
}

TEST_CASE("ReverseIterator", "[PieceTable]") {
    TempFile testFile("bizk fuoy");
    PieceTable table(testFile.path);

    testContents(table, testFile.contents);

    int count = 0;
    auto it = table.end();
    while (it != table.begin()) {
        --it;
        count++;
    }

    REQUIRE(count == testFile.contents.size());

    it = table.begin();
    REQUIRE(*it == 'b');
    ++it;
    REQUIRE(*it == 'i');
    ++it;
    REQUIRE(*it == 'z');
    --it;
    REQUIRE(*it == 'i');

    auto end = std::next(it, 3);
    REQUIRE(*end == ' ');
    REQUIRE(end != table.end());

    std::advance(it, 3);
    it = table.insert(it, 'i');

    testContents(table, "bizki fuoy");
}

TEST_CASE("Lines", "[PieceTable]") {
    TempFile testFile("abc\ndef\n\n\nghi\nfodbarbiyzbuzz");
    PieceTable table(testFile.path);

    testContents(table, testFile.contents);

    auto line = table.getLine(0);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "abc\n");

    line = table.getLine(2);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "\n");
    REQUIRE(line->size() == 1);

    line = table.getLine(3);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "\n");
    REQUIRE(line->size() == 1);
    table.erase(line->begin());

    line = table.getLine(3);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "ghi\n");
    REQUIRE(line->size() == 4);

    line = table.getLine(2);
    REQUIRE(line);
    table.erase(line->begin());

    line = table.getLine(2);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "ghi\n");

    line = table.getLine(3);
    REQUIRE(line);

    std::regex regex("barbiyz");
    using RegexIterator = std::regex_iterator<PieceTable::iterator>;
    auto regexBegin = RegexIterator(std::next(table.begin(), 2), table.end(), regex);
    auto regexEnd = RegexIterator();
    REQUIRE(regexBegin != regexEnd);
    REQUIRE(regexBegin->position() == 13);
    REQUIRE(regexBegin->length() == 7);
    REQUIRE(regexBegin->str() == "barbiyz");

    size_t traversed = 0;
    size_t lineNumber = 0;
    for (line = table.getLine(0); line; lineNumber++, line = table.getLine(lineNumber)) {
        fmt::print(stderr,
                   "Traversed {} Line {} TotalSize {} Position {}\n",
                   traversed,
                   lineNumber,
                   line->size(),
                   regexBegin->position());
        if (traversed + line->size() > regexBegin->position()) {
            break;
        }
        traversed += line->size();
    }

    REQUIRE(line);
    REQUIRE(lineNumber == 3);
    REQUIRE(traversed == 12);
    auto colStart = regexBegin->position() - traversed + 2;
    auto colEnd = colStart + regexBegin->length() - 1;
    auto it = std::next(line->begin(), colStart);
    REQUIRE(*it == 'b');
    it = std::next(line->begin(), colEnd);
    REQUIRE(*it == 'z');

    ++regexBegin;
    REQUIRE(regexBegin == regexEnd);

    line = table.getLine(4);
    REQUIRE(!line);

    line = table.getLine(1);
    REQUIRE(line);
    auto toErase = line->begin();
    ++toErase;
    table.erase(toErase);

    line = table.getLine(1);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "df\n");

    line = table.getLine(0);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "abc\n");
}