#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>
#include <vcpkg/base/util.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <vcpkg-test/util.h>

#if defined(_MSC_VER)
#pragma warning(disable : 6237)
#endif

using namespace vcpkg;
using namespace vcpkg::Paragraphs;
using namespace vcpkg::Test;

static Json::Object parse_json_object(StringView sv)
{
    auto json = Json::parse(sv);
    // we're not testing json parsing here, so just fail on errors
    if (auto r = json.get())
    {
        return std::move(r->first.object());
    }
    else
    {
        Checks::exit_with_message(VCPKG_LINE_INFO, json.error()->format());
    }
}

static Parse::ParseExpected<SourceControlFile> test_parse_manifest(StringView sv, bool expect_fail = false)
{
    auto object = parse_json_object(sv);
    auto res = SourceControlFile::parse_manifest_file(fs::u8path("<test manifest>"), object);
    if (!res.has_value() && !expect_fail)
    {
        print_error_message(res.error());
    }
    REQUIRE(res.has_value() == !expect_fail);
    return res;
}

TEST_CASE ("manifest construct minimum", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8"
    })json");

    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.empty());
}

TEST_CASE ("manifest versioning", "[manifests]")
{
    std::tuple<StringLiteral, Versions::Scheme, StringLiteral> data[] = {
        {R"json({
    "name": "zlib",
    "version-string": "abcd"
}
)json",
         Versions::Scheme::String,
         "abcd"},
        {R"json({
    "name": "zlib",
    "version-date": "2020-01-01"
}
)json",
         Versions::Scheme::Date,
         "2020-01-01"},
        {R"json({
    "name": "zlib",
    "version": "1.2.3.4.5"
}
)json",
         Versions::Scheme::Relaxed,
         "1.2.3.4.5"},
        {R"json({
    "name": "zlib",
    "version-semver": "1.2.3-rc3"
}
)json",
         Versions::Scheme::Semver,
         "1.2.3-rc3"},
    };
    for (auto v : data)
    {
        auto m_pgh = test_parse_manifest(std::get<0>(v));

        REQUIRE(m_pgh.has_value());
        auto& pgh = **m_pgh.get();
        REQUIRE(Json::stringify(serialize_manifest(pgh), Json::JsonStyle::with_spaces(4)) == std::get<0>(v));
        REQUIRE(pgh.core_paragraph->version_scheme == std::get<1>(v));
        REQUIRE(pgh.core_paragraph->version == std::get<2>(v));
    }

    test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "abcd",
        "version-semver": "1.2.3-rc3"
    })json",
                        true);
}

TEST_CASE ("manifest construct maximum", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "s",
        "version-string": "v",
        "maintainers": ["m"],
        "description": "d",
        "dependencies": ["bd"],
        "default-features": ["df"],
        "features": [
            {
                "name": "iroh",
                "description": "zuko's uncle",
                "dependencies": [
                    "firebending",
                    {
                        "name": "tea"
                    },
                    {
                        "name": "order.white-lotus",
                        "features": [ "the-ancient-ways" ],
                        "platform": "!(windows & arm)"
                    }
                ]
            },
            {
                "name": "zuko",
                "description": ["son of the fire lord", "firebending ??????"]
            }
        ]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "s");
    REQUIRE(pgh.core_paragraph->version == "v");
    REQUIRE(pgh.core_paragraph->maintainers.size() == 1);
    REQUIRE(pgh.core_paragraph->maintainers[0] == "m");
    REQUIRE(pgh.core_paragraph->description.size() == 1);
    REQUIRE(pgh.core_paragraph->description[0] == "d");
    REQUIRE(pgh.core_paragraph->dependencies.size() == 1);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "bd");
    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "df");

    REQUIRE(pgh.feature_paragraphs.size() == 2);

    REQUIRE(pgh.feature_paragraphs[0]->name == "iroh");
    REQUIRE(pgh.feature_paragraphs[0]->description.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->description[0] == "zuko's uncle");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies.size() == 3);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[0].name == "firebending");

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].name == "order.white-lotus");
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features.size() == 1);
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].features[0] == "the-ancient-ways");
    REQUIRE_FALSE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "arm"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", ""}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));
    REQUIRE(pgh.feature_paragraphs[0]->dependencies[1].platform.evaluate(
        {{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}, {"VCPKG_TARGET_ARCHITECTURE", "x86"}}));

    REQUIRE(pgh.feature_paragraphs[0]->dependencies[2].name == "tea");

    REQUIRE(pgh.feature_paragraphs[1]->name == "zuko");
    REQUIRE(pgh.feature_paragraphs[1]->description.size() == 2);
    REQUIRE(pgh.feature_paragraphs[1]->description[0] == "son of the fire lord");
    REQUIRE(pgh.feature_paragraphs[1]->description[1] == "firebending ??????");
}

TEST_CASE ("SourceParagraph manifest two dependencies", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": ["z", "openssl"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "z");
}

TEST_CASE ("SourceParagraph manifest three dependencies", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": ["z", "openssl", "xyz"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->dependencies.size() == 3);
    // should be ordered
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "openssl");
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "xyz");
    REQUIRE(pgh.core_paragraph->dependencies[2].name == "z");
}

TEST_CASE ("SourceParagraph manifest construct qualified dependencies", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "zlib",
        "version-string": "1.2.8",
        "dependencies": [
            {
                "name": "liba",
                "platform": "windows"
            },
            {
                "name": "libb",
                "platform": "uwp"
            }
        ]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->name == "zlib");
    REQUIRE(pgh.core_paragraph->version == "1.2.8");
    REQUIRE(pgh.core_paragraph->maintainers.empty());
    REQUIRE(pgh.core_paragraph->description.empty());
    REQUIRE(pgh.core_paragraph->dependencies.size() == 2);
    REQUIRE(pgh.core_paragraph->dependencies[0].name == "liba");
    REQUIRE(pgh.core_paragraph->dependencies[0].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE(pgh.core_paragraph->dependencies[1].name == "libb");
    REQUIRE(pgh.core_paragraph->dependencies[1].platform.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "WindowsStore"}}));
}

TEST_CASE ("SourceParagraph manifest default features", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "default-features": ["a1"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->default_features.size() == 1);
    REQUIRE(pgh.core_paragraph->default_features[0] == "a1");
}

TEST_CASE ("SourceParagraph manifest description paragraph", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "description": ["line 1", "line 2", "line 3"]
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->description.size() == 3);
    REQUIRE(pgh.core_paragraph->description[0] == "line 1");
    REQUIRE(pgh.core_paragraph->description[1] == "line 2");
    REQUIRE(pgh.core_paragraph->description[2] == "line 3");
}

TEST_CASE ("SourceParagraph manifest supports", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": "!(windows | osx)"
    })json");
    REQUIRE(m_pgh.has_value());
    auto& pgh = **m_pgh.get();

    REQUIRE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Linux"}}));
    REQUIRE_FALSE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", ""}}));
    REQUIRE_FALSE(pgh.core_paragraph->supports_expression.evaluate({{"VCPKG_CMAKE_SYSTEM_NAME", "Darwin"}}));
}

TEST_CASE ("SourceParagraph manifest empty supports", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": ""
    })json",
                                     true);
    REQUIRE_FALSE(m_pgh.has_value());
}

TEST_CASE ("SourceParagraph manifest non-string supports", "[manifests]")
{
    auto m_pgh = test_parse_manifest(R"json({
        "name": "a",
        "version-string": "1.0",
        "supports": true
    })json",
                                     true);
    REQUIRE_FALSE(m_pgh.has_value());
}

TEST_CASE ("Serialize all the ports", "[manifests]")
{
    std::vector<std::string> args_list = {"format-manifest"};
    auto& fs = Files::get_real_filesystem();
    auto args = VcpkgCmdArguments::create_from_arg_sequence(args_list.data(), args_list.data() + args_list.size());
    VcpkgPaths paths{fs, args};

    std::vector<SourceControlFile> scfs;

    for (auto dir : fs::directory_iterator(paths.ports))
    {
        const auto control = dir / fs::u8path("CONTROL");
        const auto manifest = dir / fs::u8path("vcpkg.json");
        if (fs.exists(control))
        {
            auto contents = fs.read_contents(control, VCPKG_LINE_INFO);
            auto pghs = Paragraphs::parse_paragraphs(contents, fs::u8string(control));
            REQUIRE(pghs);

            scfs.push_back(std::move(*SourceControlFile::parse_control_file(
                                          fs::u8string(control), std::move(pghs).value_or_exit(VCPKG_LINE_INFO))
                                          .value_or_exit(VCPKG_LINE_INFO)));
        }
        else if (fs.exists(manifest))
        {
            std::error_code ec;
            auto contents = Json::parse_file(fs, manifest, ec);
            REQUIRE_FALSE(ec);
            REQUIRE(contents);

            auto scf = SourceControlFile::parse_manifest_file(manifest,
                                                              contents.value_or_exit(VCPKG_LINE_INFO).first.object());
            REQUIRE(scf);

            scfs.push_back(std::move(*scf.value_or_exit(VCPKG_LINE_INFO)));
        }
    }

    for (auto& scf : scfs)
    {
        auto serialized = serialize_manifest(scf);
        auto serialized_scf = SourceControlFile::parse_manifest_file({}, serialized).value_or_exit(VCPKG_LINE_INFO);

        REQUIRE(*serialized_scf == scf);
    }
}
