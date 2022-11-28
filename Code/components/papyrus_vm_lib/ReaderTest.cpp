#include <gtest/gtest.h>
#include <Reader.h>

namespace
{

TEST(ReaderTests, AAATestObject)
{
    std::vector<std::string> paths{"Scripts/AAATestObject.pex"};
    Reader reader(paths);

    auto structs = reader.GetSourceStructures();
    ASSERT_EQ(structs.size(), 1);

    auto structure = structs[0];
    ASSERT_EQ(structure->header.Signature, ScriptHeader::kSignature);
}

class AAATestObjectTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        std::vector<std::string> paths{"Scripts/AAATestObject.pex"};
        s_reader = TiltedPhoques::MakeUnique<Reader>(paths);
    }

    std::shared_ptr<PexScript> GetScript()
    {
        return s_reader->GetSourceStructures()[0];
    }

    static UniquePtr<Reader> s_reader;
};

UniquePtr<Reader> AAATestObjectTest::s_reader = nullptr;

TEST_F(AAATestObjectTest, ParseSourceString)
{
    auto script = AAATestObjectTest::GetScript();

    ASSERT_STREQ(script->source.c_str(), "AAATestObject");
}

TEST_F(AAATestObjectTest, ParseObjectTable)
{
    auto script = AAATestObjectTest::GetScript();

    ASSERT_EQ(script->objectTable.size(), 1);

    auto& object = script->objectTable[0];

    ASSERT_STREQ(object.NameIndex.c_str(), "AAATestObject");
}

TEST_F(AAATestObjectTest, ParseVariables)
{
    auto script = AAATestObjectTest::GetScript();

    auto& variables = script->objectTable[0].variables;
    ASSERT_EQ(variables.size(), 1);

    auto& variable = variables[0];
    ASSERT_STREQ(variable.name.c_str(), "parentVariable");
    ASSERT_STREQ(variable.typeName.c_str(), "Int");
}

}
