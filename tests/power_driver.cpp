#include "internal.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw mns::Error("usage: test_power_driver INPUT.json");
        const auto root = mns::Json::parse(mns::read_file(argv[1]));
        mns::Master master{root.at("master_tables"), mns::Json::object()};
        mns::Json output = mns::Json::array();
        for (const auto &request : root.at("requests"))
            output.push_back(mns::resolve_deck(master, request));
        std::cout << output.dump() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
