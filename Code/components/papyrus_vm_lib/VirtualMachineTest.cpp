#include <gtest/gtest.h>
#include <VirtualMachine.h>
#include <Reader.h>

namespace
{

TEST(VirtualMachineTests, AAATestObject)
{
    std::vector<std::string> paths{"Scripts/AAATestObject.pex"};
    Reader reader(paths);
    VirtualMachine vm(reader.GetSourceStructures());

    auto pex = vm.GetPexByName("AAATestObject");
    auto script = pex.fn();

    ASSERT_STREQ(script->source.c_str(), "AAATestObject");
}

}
