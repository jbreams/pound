#define CATCH_CONFIG_MAIN

#include "piecetable.h"
#include <catch2/catch.hpp>

#include <iostream>

void dumpTable(PieceTable& table) {
    std::cerr << "Dumping table: \n";
    for (const auto& piece : table.table()) {
        stdx::string_view contents;
        switch (piece.type) {
            case PieceTable::Piece::Original:
                contents = table.originalFileView().substr(piece.start, piece.length);
                break;
            case PieceTable::Piece::AddBuffer:
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
    const auto size = table.size();
    REQUIRE(size == match.size());
    std::string str;
    std::copy(table.begin(), table.end(), std::back_inserter(str));
    REQUIRE(str == match);

    str.clear();
    std::copy(table.begin(), table.end(), std::back_inserter(str));
    REQUIRE(str == match);
}

std::string dumpLine(const PieceTable::Line& line) {
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

    auto it = table.begin();
    it += 4;
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

    auto it = table.begin();
    it += 4;
    table.erase(it);

    testContents(table, "bizzbuzz"_sv);

    it = table.begin();
    it += 6;
    table.erase(it);

    testContents(table, "bizzbuz"_sv);

    table.erase(table.begin());
    testContents(table, "izzbuz"_sv);

    it = table.begin();
    table.insert(it, 'f');

    it = table.begin();
    it += 7;
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

    auto end = it;
    end += 3;
    REQUIRE(*end == ' ');
    REQUIRE(end != table.end());

    it += 3;
    it = table.insert(it, 'i');
    auto newEnd = table.begin();
    newEnd += table.size() - 2;
    auto newBegin = it;
    newBegin -= 2;
    while (newBegin != newEnd) {
        ++newBegin;
    }

    testContents(table, "bizki fuoy");
}

TEST_CASE("Lines", "[PieceTable]") {
    TempFile testFile("abc\ndef\n\nghi\nfoobarbizzbuzz");
    PieceTable table(testFile.path);

    testContents(table, testFile.contents);

    auto line = table.getLine(0);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "abc");

    line = table.getLine(2);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "");

    line = table.getLine(3);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "ghi");

    line = table.getLine(2);
    REQUIRE(line);
    table.erase(line->begin());

    line = table.getLine(2);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "ghi");

    line = table.getLine(3);
    REQUIRE(line);
    std::regex regex("barbizz");
    using RegexIterator = std::regex_iterator<PieceTable::iterator>;
    auto regexBegin = RegexIterator(line->begin(), line->end(), regex);
    auto regexEnd = RegexIterator();
    REQUIRE(regexBegin != regexEnd);
    REQUIRE(regexBegin->position() == 3);
    REQUIRE(regexBegin->length() == 7);
    REQUIRE(regexBegin->str() == "barbizz");
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
    REQUIRE(dumpLine(*line) == "df");

    line = table.getLine(0);
    REQUIRE(line);
    REQUIRE(dumpLine(*line) == "abc");
}