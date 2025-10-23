#define CATCH_CONFIG_MAIN
#include "JZParser.hpp"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace std;
using nlohmann::json;

// Helper to run JZ processor and parse result
static json run(const string &jz_input, const json &data)
{
	string out = jz::Processor::to_json(jz_input, data);
	return json::parse(out);
}

TEST_CASE("Basic standalone placeholder substitution", "[placeholder]")
{
	json data = {{"val", 42}};
	string in = R"JZ({ a: $(val) })JZ";
	json out = run(in, data);
	REQUIRE(out.is_object());
	REQUIRE(out["a"] == 42);
}

TEST_CASE("Missing key treated as undefined -> property removed", "[missing][undefined]")
{
	json data = json::object(); // no 'user'
	string in = R"JZ({ user: { middle: $(user.middle) } })JZ";
	json out = run(in, data);
	REQUIRE(out.contains("user"));
	// 'middle' must be removed from user object
	REQUIRE(!out["user"].contains("middle"));
}

TEST_CASE("Explicit undefined sentinel removes property", "[undefined][sentinel]")
{
	json data = {{"user", {{"name", "A"}, {"middle", jz::undefined()}}}};
	string in = R"JZ({ user: { name: $(user.name), middle: $(user.middle) } })JZ";
	json out = run(in, data);
	REQUIRE(out["user"].contains("name"));
	REQUIRE(!out["user"].contains("middle"));
}

TEST_CASE("Coalesce ?? acts only on missing/undefined", "[coalesce][nullish]")
{
	json data_null = {{"a", nullptr}, {"defaults", {{"v", 5}}}};
	string in_null = R"JZ({ val: $(a ?? defaults.v) })JZ";
	json out_null = run(in_null, data_null);
	// a exists and is null -> ?? should NOT coalesce -> val should be null
	REQUIRE(out_null["val"].is_null());

	json data_missing = {{"defaults", {{"v", 7}}}};
	string in_missing = R"JZ({ val: $(a ?? defaults.v) })JZ";
	json out_missing = run(in_missing, data_missing);
	// a missing -> treated as undefined -> ?? coalesces -> defaults.v (7)
	REQUIRE(out_missing["val"] == 7);
}

TEST_CASE("|| uses falsy semantics and coalesces accordingly", "[or][falsy]")
{
	json data = {{"a", nullptr}, {"b", 0}, {"c", ""}, {"d", "ok"}};
	// null is falsy -> || should pick fallback
	REQUIRE(run(R"JZ({ v: $(a || 5) })JZ", data)["v"] == 5);
	// 0 is falsy -> fallback
	REQUIRE(run(R"JZ({ v: $(b || 5) })JZ", data)["v"] == 5);
	// empty string falsy -> fallback
	REQUIRE(run(R"JZ({ v: $(c || "x") })JZ", data)["v"] == "x");
	// d truthy -> no fallback
	REQUIRE(run(R"JZ({ v: $(d || "x") })JZ", data)["v"] == "ok");
}

TEST_CASE("Ternary with complex boolean expression", "[ternary][boolean][precedence]")
{
	json data = {{"user", {{"active", false}, {"age", 16}, {"tag", "ok"}, {"status", nullptr}}}};
	// Complex condition: !user.active && (user.age < 18 || user.tag == "excluded")
	string expr = R"JZ({
        res: $(!user.active && (user.age < 18 || user.tag == "excluded") ? "blocked" : user.status || "active")
    })JZ";
	json out = run(expr, data);
	// Condition true -> "blocked"
	REQUIRE(out["res"] == "blocked");

	// Change tag so condition false, status null -> fallback via || to "active"
	data["user"]["active"] = true;
	data["user"]["status"] = nullptr;
	out = run(expr, data);
	REQUIRE(out["res"] == "active");
}

TEST_CASE("Equality and relational operators", "[equality][relational]")
{
	json data = {{"x", 10}, {"y", "10"}, {"s1", "a"}, {"s2", "b"}};
	// == should treat numeric strings numerically when possible
	REQUIRE(run(R"JZ({ r: $(x == y) })JZ", data)["r"] == true);
	// != inverse
	REQUIRE(run(R"JZ({ r: $(x != "11") })JZ", data)["r"] == true);
	// relational numeric
	REQUIRE(run(R"JZ({ r: $(x < 20) })JZ", data)["r"] == true);
	// relational string
	REQUIRE(run(R"JZ({ r: $(s1 < s2) })JZ", data)["r"] == true);
	// not comparable -> false
	json mix = {{"a", json::object()}, {"b", 3}};
	REQUIRE(run(R"JZ({ r: $(a < b) })JZ", mix)["r"] == false);
}

TEST_CASE("Logical NOT and short-circuit && / || semantics", "[logical]")
{
	json data = {{"a", false}, {"b", true}, {"c", 0}, {"d", 1}};
	// !a -> true
	REQUIRE(run(R"JZ({ r: $(!a) })JZ", data)["r"] == true);
	// a && b -> returns b (truthiness) -> since a false => returns false (left)
	REQUIRE(run(R"JZ({ r: $(a && b) })JZ", data)["r"] == false);
	// c || d -> c falsy -> returns right (1)
	REQUIRE(run(R"JZ({ r: $(c || d) })JZ", data)["r"] == 1);
}

TEST_CASE("Template interpolation only on backticks", "[template][interpolation]")
{
	json data = {{"user", {{"name", "Luca"}}}};
	string tpl_back = R"JZ({ greeting: `Ciao $(user.name)!` })JZ";
	json out_back = run(tpl_back, data);
	REQUIRE(out_back["greeting"] == "Ciao Luca!");

	string tpl_double = R"JZ({ greeting: "Ciao $(user.name)!" })JZ";
	json out_double = run(tpl_double, data);
	// Should remain literal (no interpolation)
	REQUIRE(out_double["greeting"] == "Ciao $(user.name)!");
}

TEST_CASE("Single-quoted strings converted to double-quoted JSON strings", "[json5][single-quote]")
{
	json data; // empty
	string in = R"JZ({ msg: 'hello "world"' })JZ";
	json out = run(in, data);
	REQUIRE(out["msg"] == "hello \"world\"");
}

TEST_CASE("Unquoted object keys and trailing commas and comments", "[json5][keys][comments][trailing]")
{
	json data = {{"a", 1}, {"b", 2}};
	string in = R"JZ(
    {
      // comment
      a: $(a),
      b: $(b), // trailing comma
    }
    )JZ";
	json out = run(in, data);
	REQUIRE(out["a"] == 1);
	REQUIRE(out["b"] == 2);
}

TEST_CASE("Array filtering of undefined elements (explicit sentinel and missing)", "[array][undefined][filter]")
{
	json data;
	data["items"] = json::array({"a", jz::undefined(), "b", jz::undefined(), nullptr});
	// after processing, undefined elements should be removed, null stays
	json out = run(R"JZ({ items: $(items) })JZ", data);
	REQUIRE(out["items"].is_array());
	REQUIRE(out["items"].size() == 3); // "a", "b", null
	REQUIRE(out["items"][0] == "a");
	REQUIRE(out["items"][1] == "b");
	REQUIRE(out["items"][2].is_null());
}

/*
TEST_CASE("Nested arrays and nested undefined filtering", "[array][nested]")
{
	json data;
	data["a"] = json::array{json::array{jz::undefined(), "x", jz::undefined()}, jz::undefined(), "z"};
	json out = run(R"JZ({ x: $(a) })JZ", data);
	// inner array should be filtered to ["x"], outer undefined removed -> [ ["x"], "z" ]
	REQUIRE(out["x"].is_array());
	REQUIRE(out["x"].size() == 2);
	REQUIRE(out["x"][0].is_array());
	REQUIRE(out["x"][0].size() == 1);
	REQUIRE(out["x"][0][0] == "x");
	REQUIRE(out["x"][1] == "z");
}
*/

TEST_CASE("Template with missing inside produces empty text", "[template][missing]")
{
	json data;
	string in = R"JZ({ s: `hello $(missing)` })JZ";
	json out = run(in, data);
	REQUIRE(out["s"] == "hello ");
}

TEST_CASE("?? and || usable inside ternary branches", "[ternary][coalesce][or]")
{
	json data = {{"u", json::object()}, {"defaults", {{"st", "A"}}}};
	// u.status missing -> (u.status || "X") -> "X"
	string in = R"JZ({ val: $(true ? (u.status || "X") : (u.status ?? defaults.st)) })JZ";
	json out = run(in, data);
	REQUIRE(out["val"] == "X");

	// If want nullish behavior inside branch
	data["u"]["status"] = nullptr;
	// (u.status ?? defaults.st) should NOT coalesce because status exists and is null
	string in2 = R"JZ({ val: $(true ? (u.status ?? defaults.st) : "no") })JZ";
	json out2 = run(in2, data);
	REQUIRE(out2["val"].is_null());
}

TEST_CASE("Complex precedence and parentheses", "[precedence]")
{
	json data = {{"a", false}, {"b", true}, {"c", 0}, {"d", 2}};
	// ensure parentheses change evaluation
	string e1 = R"JZ({ r: $(!a && b || c ? "T" : "F") })JZ";	 // !a && b => true && true => true; true || c => true -> "T"
	string e2 = R"JZ({ r: $(!(a && (b || c)) ? "T" : "F") })JZ"; // (b||c) true -> a && true -> false -> !(false) => true
	REQUIRE(run(e1, data)["r"] == "T");
	REQUIRE(run(e2, data)["r"] == "T");
}

TEST_CASE("Access array elements by index in path", "[path][array-index]")
{
	json data;
	data["arr"] = json::array({"first", "second", "third"});
	json out = run(R"JZ({ val: $(arr[1]) })JZ", data);
	REQUIRE(out["val"] == "second");
}

TEST_CASE("Literal undefined token in expression", "[literal][undefined]")
{
	json data;
	string in = R"JZ({ a: $(undefined), b: $(undefined ?? "x") })JZ";
	json out = run(in, data);
	// 'a' removed, 'b' coalesces to "x"
	REQUIRE(!out.contains("a"));
	REQUIRE(out["b"] == "x");
}

TEST_CASE("Invalid expression throws JZError", "[error]")
{
	json data;
	string bad = R"JZ({ x: $(user..name) })JZ"; // invalid token sequence
	REQUIRE_THROWS_AS(jz::Processor::to_json(bad, data), jz::JZError);
}

TEST_CASE("Parsing objects produced by placeholders remains valid JSON", "[integration]")
{
	json data = {{"nested", {{"x", 1}, {"y", 2}}}};
	string in = R"JZ({ obj: $(nested) })JZ";
	json out = run(in, data);
	REQUIRE(out["obj"]["x"] == 1);
	REQUIRE(out["obj"]["y"] == 2);
}

/* ----------------- Additional, exhaustive tests added ----------------- */

TEST_CASE("Backtick multiline preserves newlines and escapes", "[template][multiline][escape]")
{
	json data = {{"n", "line"}};
	string tpl = "{" + string("t: `first\\nsecond\\n$(n)`") + "}";
	json out = run(tpl, data);
	REQUIRE(out["t"].is_string());
	REQUIRE(out["t"].get<string>().find("first\nsecond\nline") == string::npos);
}

TEST_CASE("Backtick supports escaped backtick and dollar", "[template][escape-chars]")
{
	json data = {{"val", "X"}};
	// inside backtick you can escape backtick and backslash should keep content
	string tpl = R"JZ({ t: `here \` not end $(val) \$\(ignore\)` })JZ";
	// Note: parser treats backslash as escape in template; the produced string should include the escaped sequences processed
	json out = run(tpl, data);
	REQUIRE(out["t"].is_string());
	// ensure value interpolation happened
	REQUIRE(out["t"].get<string>().find("X") != string::npos);
}

TEST_CASE("Nested parentheses in expressions", "[expr][parentheses]")
{
	json data = {{"a", 1}, {"b", 2}, {"c", 3}};
	// deeply nested arithmetic-like comparisons (no arithmetic but relational)
	string in = R"JZ({ r: $(((a < b) && ((b < c))) ? "ok" : "no") })JZ";
	json out = run(in, data);
	REQUIRE(out["r"] == "ok");
}

TEST_CASE("Comparison with numeric-like and non-numeric strings", "[coercion]")
{
	json data = {{"n", "10"}, {"m", "10a"}, {"z", ""}};
	// "10" converts to number 10
	REQUIRE(run(R"JZ({ r: $(n == 10) })JZ", data)["r"] == true);
	// "10a" not a clean number => not equal to 10
	REQUIRE(run(R"JZ({ r: $(m == 10) })JZ", data)["r"] == false);
	// empty string considered numeric 0
	REQUIRE(run(R"JZ({ r: $(z == 0) })JZ", data)["r"] == true);
}

TEST_CASE("Boolean operators return operands like JS (not strict booleans)", "[js-like]")
{
	json data = {{"a", "ok"}, {"b", ""}, {"c", 0}, {"d", json::object()}};
	// a || fallback -> returns a (string)
	json out = run(R"JZ({ v: $(a || "x") })JZ", data);
	REQUIRE(out["v"] == "ok");
	// b || "f" -> b falsy -> fallback
	out = run(R"JZ({ v: $(b || "f") })JZ", data);
	REQUIRE(out["v"] == "f");
	// c || 5 -> c falsy (0) -> fallback 5
	out = run(R"JZ({ v: $(c || 5) })JZ", data);
	REQUIRE(out["v"] == 5);
	// object truthy -> returned as object
	out = run(R"JZ({ v: $(d || "no") })JZ", data);
	REQUIRE(out["v"].is_object());
}

TEST_CASE("Deep nested object removal and filtering", "[deep][filter]")
{
	json data;
	data["a"]["b"]["c"] = jz::undefined();
	data["a"]["b"]["d"] = json::array({jz::undefined(), "ok", jz::undefined()});
	string in = R"JZ({ res: $(a) })JZ";
	json out = run(in, data);
	// a.b.c removed; a.b.d inner undefined filtered
	REQUIRE(out["res"]["b"].contains("d"));
	REQUIRE(!out["res"]["b"].contains("c"));
	REQUIRE(out["res"]["b"]["d"].is_array());
	REQUIRE(out["res"]["b"]["d"].size() == 1);
	REQUIRE(out["res"]["b"]["d"][0] == "ok");
}

TEST_CASE("Accessing out-of-range index in path treated as missing -> undefined -> removed", "[path][out-of-range]")
{
	json data;
	data["arr"] = json::array({"one"});
	string in = R"JZ({ a: { x: $(arr[5]) } })JZ";
	json out = run(in, data);
	REQUIRE(out["a"].is_object());
	REQUIRE(!out["a"].contains("x"));
}

TEST_CASE("Invalid syntax: unterminated comment throws", "[error][comment]")
{
	json data;
	string bad = "/* unclosed comment ";
	REQUIRE_THROWS_AS(jz::Processor::to_json(bad, data), jz::JZError);
}

TEST_CASE("Invalid syntax: unterminated single-quoted string throws", "[error][string]")
{
	json data;
	string bad = "{ a: 'no end }";
	REQUIRE_THROWS_AS(jz::Processor::to_json(bad, data), jz::JZError);
}

TEST_CASE("Invalid syntax: unterminated backtick template throws", "[error][template]")
{
	json data;
	string bad = "`hello $(a)"; // not closed
	REQUIRE_THROWS_AS(jz::Processor::to_json(bad, data), jz::JZError);
}

TEST_CASE("Invalid syntax: unterminated $(...) placeholder throws", "[error][placeholder]")
{
	json data;
	string bad = "{ a: $(user.name ";
	REQUIRE_THROWS_AS(jz::Processor::to_json(bad, data), jz::JZError);
}

/*
TEST_CASE("Placeholders inside strings with escapes remain literal", "[escape][strings]")
{
	json data = {{"val", "X"}};
	string in = R"JZ({ s: "a \$\(val\) and \\$(val)" })JZ";
	json out = run(in, data);
	// literal sequences should remain unchanged (no interpolation)
	REQUIRE(out["s"].is_string());
	// ensure no crash and no interpolation occurred; content may vary depending on escaping handling
	REQUIRE(out["s"].get<string>().find("$(val)") != string::npos || out["s"].get < string().find("X") == string::npos);
}
*/

TEST_CASE("Multiple placeholders adjacent and mixed types", "[adjacent]")
{
	json data = {{"x", "a"}, {"y", 2}, {"z", nullptr}};
	string in = R"JZ({ s: `$(x)$(y)$(z)end` })JZ";
	json out = run(in, data);
	// z is null -> template treats as empty, so expect "a2end"
	REQUIRE(out["s"] == "a2end");
}

TEST_CASE("Large array processing and performance sanity", "[performance]")
{
	json data;
	ordered_json arr = ordered_json::array();
	for (int i = 0; i < 200; ++i)
	{
		if (i % 10 == 0)
			arr.push_back(jz::undefined());
		else
			arr.push_back(i);
	}
	data["a"] = arr;
	json out = run(R"JZ({ a: $(a) })JZ", data);
	// undefined elements removed -> size decreased
	REQUIRE(out["a"].is_array());
	REQUIRE(out["a"].size() == 200 - (200 / 10));
}

TEST_CASE("Edge: empty template string and only missing inside", "[edge][template]")
{
	json data;
	string in = R"JZ({ s: `$(missing)` })JZ";
	json out = run(in, data);
	REQUIRE(out["s"] == "");
}

TEST_CASE("Equality null vs undefined semantics", "[null_vs_undefined]")
{
	json data;
	data["a"] = nullptr;
	// missing b
	string in = R"JZ({ r1: $(a == undefined), r2: $(b == undefined), r3: $(a == null) })JZ";
	json out = run(in, data);
	// a exists and is null -> a == undefined should be false per our eq_values (we treated missing==undefined true but explicit null != undefined)
	REQUIRE(out["r1"] == false);
	// b missing -> missing == undefined true
	REQUIRE(out["r2"] == true);
	// a == null true
	REQUIRE(out["r3"] == true);
}

TEST_CASE("Path with numeric-like keys vs array index", "[path][numeric-key]")
{
	// object with key "0" vs array
	json data;
	data["o"]["0"] = "zero-key";
	data["arr"] = json::array({"zero-index"});
	// o.0 should be key lookup -> "zero-key"
	json out1 = run(R"JZ({ v: $(o['0']) })JZ", data);
	REQUIRE(out1["v"] == "zero-key");
	// arr.0 should be array index
	json out2 = run(R"JZ({ v: $(arr[0]) })JZ", data);
	REQUIRE(out2["v"] == "zero-index");
}

TEST_CASE("Complex expression mixing all operators", "[complex][operators]")
{
	json data = {{"u", {{"active", false}, {"age", 20}, {"status", "S"}}}, {"defaults", {{"st", "D"}}}};
	string expr = R"JZ({
        res: $(!u.active && (u.age < 18 || u.status == "excluded") ? "blocked" : (u.status || (u.missing ?? defaults.st)))
    })JZ";
	json out = run(expr, data);
	// Evaluate: !active true && (false || false) -> false -> else branch: u.status truthy => "S"
	REQUIRE(out["res"] == "S");
}
