#include <catch2/catch_test_macros.hpp>
#include "Core/Version.hpp"
TEST_CASE("version string is set") { REQUIRE(!qlab::core::version().empty()); }
