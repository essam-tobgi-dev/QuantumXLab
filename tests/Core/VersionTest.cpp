#include "Core/Version.hpp"
#include <catch2/catch_test_macros.hpp>
TEST_CASE("version string is set") {
    REQUIRE(!qlab::core::version().empty());
}
