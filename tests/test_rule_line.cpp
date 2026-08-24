#include "include/database/entities/RouteProfile.h"

#include <QTest>

using Configs::NormalizeRuleLine;

class TestRuleLine : public QObject {
    Q_OBJECT

private slots:
    void emptyMatchersAreRejected();
    void suffixLosesItsLeadingDot();
    void aliasesCollapseToOneKind();
    void decorationIsStripped();
    void nonRulesAreRejected();
};

// A rule that normalises to a bare kind has nothing to match on, so it would sit in
// the profile as an action with no condition and quietly catch everything.
void TestRuleLine::emptyMatchersAreRejected() {
    QVERIFY(NormalizeRuleLine("suffix:.").isEmpty());
    QVERIFY(NormalizeRuleLine("domain_suffix: .").isEmpty());
    QVERIFY(NormalizeRuleLine(".").isEmpty());
    QVERIFY(NormalizeRuleLine("suffix:").isEmpty());
}

void TestRuleLine::suffixLosesItsLeadingDot() {
    QCOMPARE(NormalizeRuleLine("suffix:.example.com"), QStringLiteral("suffix:example.com"));
    QCOMPARE(NormalizeRuleLine(".example.com"), QStringLiteral("suffix:example.com"));
}

void TestRuleLine::aliasesCollapseToOneKind() {
    QCOMPARE(NormalizeRuleLine("domain_suffix:example.com"), QStringLiteral("suffix:example.com"));
    QCOMPARE(NormalizeRuleLine("full:example.com"), QStringLiteral("domain:example.com"));
    QCOMPARE(NormalizeRuleLine("rule_set:geosite-ru-blocked"), QStringLiteral("ruleset:geosite-ru-blocked"));
}

void TestRuleLine::decorationIsStripped() {
    QCOMPARE(NormalizeRuleLine("  - \"example.com\",  "), QStringLiteral("domain:example.com"));
}

void TestRuleLine::nonRulesAreRejected() {
    QVERIFY(NormalizeRuleLine("# a comment").isEmpty());
    QVERIFY(NormalizeRuleLine("// also a comment").isEmpty());
    QVERIFY(NormalizeRuleLine("   ").isEmpty());
    QVERIFY(NormalizeRuleLine("two words").isEmpty());
}

QTEST_GUILESS_MAIN(TestRuleLine)
#include "test_rule_line.moc"
