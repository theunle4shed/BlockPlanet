#include "tetrisphere/developer_records.h"

#include <cassert>
#include <string>

int main() {
    using tetrisphere::DeveloperRecords;
    DeveloperRecords d;
    // Times: lower is better; scores: higher.
    assert(d.record("time_attack", 1, 1, 50.0));
    assert(!d.record("time_attack", 1, 1, 60.0) && d.record("time_attack", 1, 1, 45.0));
    assert(d.value("time_attack", 1, 1) == 45.0 && d.value("time_attack", 1, 2) == 0.0);
    assert(d.record("score_attack", 2, 3, 1000.0) && !d.record("score_attack", 2, 3, 900.0));
    assert(!d.record("time_attack", 11, 1, 10.0) && !d.record("time_attack", 1, 11, 10.0));
    assert(DeveloperRecords::beats("time_attack", 45.0, 45.0) && !DeveloperRecords::beats("time_attack", 46.0, 45.0));
    assert(DeveloperRecords::beats("score_attack", 1200.0, 1000.0) && !DeveloperRecords::beats("score_attack", 0.0, 1.0));

    // All beaten needs every stage recorded and beaten.
    std::map<int, std::vector<double>> bests;
    assert(!d.all_beaten("time_attack", bests));
    DeveloperRecords full;
    for (int c = 1; c <= 10; ++c) {
        for (int s = 1; s <= 10; ++s) full.record("time_attack", c, s, 60.0);
        bests[c] = std::vector<double>(10, 59.0);
    }
    assert(full.all_beaten("time_attack", bests));
    bests[10][9] = 61.0;
    assert(!full.all_beaten("time_attack", bests));
    bests[10][9] = 0.0;  // never finished
    assert(!full.all_beaten("time_attack", bests));

    // JSON round trip; the shipped empty file parses.
    DeveloperRecords back;
    std::string error;
    assert(tetrisphere::parse_developer_records(tetrisphere::developer_records_json(d), back, error));
    assert(back.value("time_attack", 1, 1) == 45.0 && back.value("score_attack", 2, 3) == 1000.0);
    assert(tetrisphere::parse_developer_records(R"({"schema_version":1,"time_attack":{},"score_attack":{}})", back, error));
    assert(!tetrisphere::parse_developer_records(R"({"time_attack":{"11":[1]}})", back, error));
    assert(!tetrisphere::parse_developer_records("nope", back, error));
    return 0;
}
