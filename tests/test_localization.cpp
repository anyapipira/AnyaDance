#include "test_framework.h"

#include "ui/localization.h"

#include <cstring>

namespace anyadance::tests {

void TestLocalization() {
    using namespace anyadance::ui;

    EXPECT_TRUE(kLanguageCount == 3);
    EXPECT_TRUE(std::strcmp(GetLanguageInfo(Language::Japanese).code, "ja-JP") == 0);
    EXPECT_TRUE(std::strcmp(GetLanguageInfo(Language::Japanese).displayName, u8"日本語") == 0);
    EXPECT_TRUE(FindLanguageByCode("ja-JP", Language::English) == Language::Japanese);
    EXPECT_TRUE(FindLanguageByCode("unknown", Language::ChineseSimplified) == Language::ChineseSimplified);

    for (std::size_t i = 0; i < kTextCount; ++i) {
        const char* japanese = Tr(static_cast<Text>(i), Language::Japanese);
        EXPECT_TRUE(japanese != nullptr);
        EXPECT_TRUE(japanese && japanese[0] != '\0');
    }

    EXPECT_TRUE(std::strcmp(Tr(Text::Reset, Language::Japanese), u8"Tポーズにリセット") == 0);
    EXPECT_TRUE(std::strcmp(DeviceName(1, Language::Japanese), u8"左コントローラー") == 0);

    const Language original = CurrentLanguage();
    SetCurrentLanguage(Language::Japanese);
    EXPECT_TRUE(CurrentLanguage() == Language::Japanese);
    EXPECT_TRUE(std::strcmp(Tr(Text::LanguageLabel), u8"言語") == 0);
    SetCurrentLanguage(original);
}

} // namespace anyadance::tests
