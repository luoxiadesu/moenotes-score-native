#include "json.hpp"
#include "moenotes_score.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
using mns::Json;
static void check(mns_status s) {
    if (s != MNS_OK)
        throw std::runtime_error(mns_last_error());
}
int main(int argc, char **argv) {
    try {
        std::string master_path, chart_path, request_path, output_path;
        uint32_t budget = 30000;
        bool progress = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::runtime_error("missing " + arg + " value");
                return std::string(argv[i]);
            };
            if (arg == "--help") {
                std::cout << "moenotes-score --master-dir DIR --chart FILE --request FILE [--budget-ms "
                             "30000] [--progress] [--output FILE]\nInputs are local. Progress is NDJSON; no "
                             "game network or device access.\n";
                return 0;
            }
            if (arg == "--version") {
                std::cout << mns_version() << '\n';
                return 0;
            }
            if (arg == "--master-dir")
                master_path = value();
            else if (arg == "--chart")
                chart_path = value();
            else if (arg == "--request")
                request_path = value();
            else if (arg == "--output")
                output_path = value();
            else if (arg == "--budget-ms") {
                auto v = std::stoll(value());
                if (v < 0 || v > 3600000)
                    throw std::runtime_error("budget must be 0..3600000ms");
                budget = uint32_t(v);
            } else if (arg == "--progress")
                progress = true;
            else
                throw std::runtime_error("unknown argument: " + arg);
        }
        if (master_path.empty() || chart_path.empty() || request_path.empty())
            throw std::runtime_error("provide --master-dir, --chart and --request; see --help");
        auto request = Json::parse(mns::read_file(request_path));
        if (!request.is_object())
            throw std::runtime_error("request must be object");
        for (auto &[k, v] : request.items()) {
            (void)v;
            if (k != "schema_version" && k != "calculation" && k != "player_data" && k != "deck_id" &&
                k != "deck" && k != "account" && k != "deck_context" && k != "options")
                throw std::runtime_error("unknown request field: " + k);
        }
        if (request.at("schema_version").get<int>() != 1)
            throw std::runtime_error("schema_version must be 1");
        Json tables = Json::object();
        size_t bytes = 0;
        for (auto &entry : std::filesystem::directory_iterator(master_path))
            if (entry.is_regular_file() && entry.path().extension() == ".json" &&
                entry.path().filename().string().starts_with("Master") &&
                entry.path().filename() != "MasterManifest.json") {
                auto raw = mns::read_file(entry.path().string());
                bytes += raw.size();
                if (bytes > 64 * 1024 * 1024)
                    throw std::runtime_error("master bundle exceeds 64MiB");
                tables[entry.path().stem().string()] = Json::parse(raw);
            }
        auto master = tables.dump();
        auto chart = mns::read_file(chart_path);
        auto calculation = request.at("calculation");
        calculation["schema_version"] = 1;
        auto prepared_request = calculation.dump();
        mns_prepared *raw_p = nullptr;
        check(mns_prepare(master.data(), master.size(), chart.data(), chart.size(), chart_path.c_str(),
                          prepared_request.data(), prepared_request.size(), &raw_p));
        std::unique_ptr<mns_prepared, decltype(&mns_prepared_free)> p(raw_p, mns_prepared_free);
        Json dr = Json::object();
        for (auto key : {"player_data", "deck_id", "deck", "account", "deck_context"})
            if (request.contains(key))
                dr[key] = request.at(key);
        auto deck_json = dr.dump();
        mns_deck *raw_d = nullptr;
        check(mns_deck_resolve(p.get(), deck_json.data(), deck_json.size(), &raw_d));
        std::unique_ptr<mns_deck, decltype(&mns_deck_free)> d(raw_d, mns_deck_free);
        auto opts = request.contains("options") ? request.at("options").dump() : "{}";
        mns_analysis *raw_a = nullptr;
        check(mns_analysis_create(p.get(), d.get(), opts.data(), opts.size(), &raw_a));
        std::unique_ptr<mns_analysis, decltype(&mns_analysis_free)> a(raw_a, mns_analysis_free);
        auto start = std::chrono::steady_clock::now();
        std::string final;
        do {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            auto left = elapsed < budget ? budget - uint32_t(elapsed) : 0;
            check(mns_analysis_advance(a.get(), std::min(left, uint32_t(1000)), UINT64_MAX));
            char *raw = nullptr;
            check(mns_analysis_snapshot(a.get(), &raw));
            final = raw;
            mns_string_free(raw);
            auto snapshot = Json::parse(final);
            if (progress)
                std::cout << final << '\n' << std::flush;
            if (snapshot.at("complete").get<bool>() ||
                snapshot.at("stop_reason").get<std::string>() == "memory_limit" ||
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                      start)
                        .count() >= budget)
                break;
        } while (true);
        if (!output_path.empty()) {
            std::ofstream out(output_path);
            if (!out || !(out << final << '\n'))
                throw std::runtime_error("cannot write output");
        } else if (!progress)
            std::cout << final << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "moenotes-score: " << e.what() << '\n';
        return 1;
    }
}
