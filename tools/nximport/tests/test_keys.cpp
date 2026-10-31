#include "keys.h"
#include "test.h"

#include <cstring>

using namespace nx;

static void parse(KeySet &k, const std::string &text) { k.parse_prod_keys(reinterpret_cast<const uint8_t *>(text.data()), text.size()); }

TEST(key_revision_formula) {
    CHECK(key_revision(0, 0) == 0);
    CHECK(key_revision(0, 1) == 0);
    CHECK(key_revision(1, 0) == 0);
    CHECK(key_revision(2, 0) == 1);
    CHECK(key_revision(0, 2) == 1);
    CHECK(key_revision(2, 3) == 2);
    CHECK(key_revision(0, 0xFF) == 0xFE);
    CHECK(key_name("key_area_key_application", 0xb) == "key_area_key_application_0b");
}

TEST(prod_keys_parse_and_lookup) {
    KeySet k;
    parse(k, "; comment\n# other comment\n\n  HEADER_KEY = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\r\n"
             "key_area_key_application_00=000102030405060708090a0b0c0d0e0f\n"
             "titlekek_0a , 0f0e0d0c0b0a09080706050403020100\n"
             "some_other_key = not even hex\n");
    CHECK(k.header_key().set);
    CHECK(k.header_key().b[1] == 0x11);
    CHECK(k.key_area_key(KAK_APPLICATION, 0).b[15] == 0x0f);
    CHECK(k.titlekek(0xa).b[0] == 0x0f);
}

TEST(prod_keys_missing_key_messages) {
    KeySet k;
    parse(k, "header_key = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n");
    std::string e = ERROR_OF(k.key_area_key(KAK_APPLICATION, 0x0b));
    CHECK(contains(e, "key_area_key_application_0b"));
    CHECK(contains(e, "older firmware"));
    CHECK(contains(e, "firmware mais antigo"));
    CHECK(contains(ERROR_OF(k.titlekek(0)), "titlekek_00"));
    KeySet empty;
    CHECK(contains(ERROR_OF(empty.header_key()), "header_key"));
}

TEST(prod_keys_errors_never_show_values) {
    const std::string secret = "c0ffeec0ffeec0ffeec0ffeec0ffee";  // 30 digits: wrong length
    for (const std::string text : {"header_key = " + secret + "\n", "titlekek_00 = " + secret + "zz\n",
                                   "key_area_key_application_00 = " + secret + "\n"}) {
        KeySet k;
        std::string e = ERROR_OF(parse(k, text));
        CHECK(!e.empty());
        CHECK(!contains(e, "c0ffee"));
        CHECK(contains(e, "line 1") || contains(e, "linha 1"));
    }
    KeySet k;
    std::string e = ERROR_OF(parse(k, "header_key\n"));
    CHECK(contains(e, "line 1"));
}

TEST(prod_keys_derivation) {
    // master_key + sources -> key area key and titlekek, as generate_kek does (the sources are random test bytes)
    Key128 master, kek_seed, key_seed, kak_src, tk_src;
    uint8_t v = 1;
    for (Key128 *k : {&master, &kek_seed, &key_seed, &kak_src, &tk_src}) {
        for (auto &b : k->b) b = v++;
        k->set = true;
    }
    std::string text = "master_key_03 = " + hex(master.data(), 16) + "\naes_kek_generation_source = " + hex(kek_seed.data(), 16) +
                       "\naes_key_generation_source = " + hex(key_seed.data(), 16) + "\nkey_area_key_application_source = " +
                       hex(kak_src.data(), 16) + "\ntitlekek_source = " + hex(tk_src.data(), 16) + "\n";
    KeySet k;
    parse(k, text);
    k.derive();
    Key128 kek, src_kek, expect, tk;
    aes_ecb_decrypt(master, kek_seed.data(), kek.data(), 16);
    aes_ecb_decrypt(kek, kak_src.data(), src_kek.data(), 16);
    aes_ecb_decrypt(src_kek, key_seed.data(), expect.data(), 16);
    aes_ecb_decrypt(master, tk_src.data(), tk.data(), 16);
    CHECK(k.key_area_key(KAK_APPLICATION, 3).b == expect.b);
    CHECK(k.titlekek(3).b == tk.b);
    CHECK(!ERROR_OF(k.key_area_key(KAK_APPLICATION, 2)).empty());  // (no master_key_02)
}

TEST(title_keys_parse) {
    KeySet k;
    std::string t = "0100000000001000000000000000000a = 00112233445566778899aabbccddeeff\n";
    k.parse_title_keys(reinterpret_cast<const uint8_t *>(t.data()), t.size());
    uint8_t rid[16] = {0x01, 0, 0, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0x0a};
    REQUIRE(k.title_key(rid));
    CHECK(k.title_key(rid)->b[15] == 0xff);
    rid[0] = 2;
    CHECK(!k.title_key(rid));
    KeySet bad;
    std::string b = "nothex = 00112233445566778899aabbccddeeff\n";
    std::string e = ERROR_OF(bad.parse_title_keys(reinterpret_cast<const uint8_t *>(b.data()), b.size()));
    CHECK(contains(e, "rights id"));
    CHECK(!contains(e, "00112233"));
}
