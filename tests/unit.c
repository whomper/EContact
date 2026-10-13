/*
 * unit.c - tests for the contacts core: vCards in (3.0 from iCloud, 4.0,
 * 2.1 from old phones) and out, labels, birthdays, groups, XML and URLs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"
#include "xml.h"
#include "http.h"
#include "charset.h"
#include "util.h"
#include "photo.h"
#include "photo_vec.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); checks++; if (_a != _b) { fails++; \
	printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); checks++; \
	if (!_a || strcmp(_a, _b)) { fails++; \
	printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b); } } while (0)

/* as iCloud sends it: folded lines, Apple's item labels, a photo */
static const char icloud_card[] =
	"BEGIN:VCARD\r\n"
	"VERSION:3.0\r\n"
	"PRODID:-//Apple Inc.//iPhone OS 17.0//EN\r\n"
	"N:Yaary;Erez;;Dr.;\r\n"
	"FN:Dr. Erez Yaary\r\n"
	"ORG:Atari Club;Hardware\r\n"
	"TITLE:Chief Tinkerer\r\n"
	"EMAIL;type=INTERNET;type=HOME;type=pref:erez@example.com\r\n"
	"item1.EMAIL;type=INTERNET:erez@club.example.org\r\n"
	"item1.X-ABLabel:club\r\n"
	"TEL;type=CELL;type=VOICE;type=pref:+972 50 123 4567\r\n"
	"TEL;type=IPHONE;type=CELL;type=VOICE:+972 52 765 4321\r\n"
	"item2.TEL:03-555 1234\r\n"
	"item2.X-ABLabel:_$!<HomeFAX>!$_\r\n"
	"item3.ADR;type=HOME;type=pref:;;1 Herzl St\\nApt 4;Tel Aviv;;6100000;Israel\r\n"
	"item3.X-ABADR:il\r\n"
	"item4.URL;type=pref:https://example.com/erez\r\n"
	"item4.X-ABLabel:_$!<HomePage>!$_\r\n"
	"item5.X-ABRELATEDNAMES:Dana\r\n"
	"item5.X-ABLabel:_$!<Spouse>!$_\r\n"
	"BDAY;value=date:1975-05-17\r\n"
	"NOTE:Met at the Atari meet\\, 1989.\\nLikes Falcons.\r\n"
	"PHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgK\r\n"
	" DBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJ\r\n"
	"X-SOCIALPROFILE;type=twitter:x-apple:erezyaary\r\n"
	"UID:7B5D2E1A-0C3F-4A8B-9E21-55AA77CC1234\r\n"
	"END:VCARD\r\n";

static void test_parse_icloud(void)
{
	CONTACT *c = vcard_parse(icloud_card, (long)strlen(icloud_card));
	char s[100];
	CHECK(c != 0);
	if (!c)
		return;
	CHECK_STR(c->family, "Yaary");
	CHECK_STR(c->given, "Erez");
	CHECK_STR(c->prefix, "Dr.");
	CHECK_STR(c->fn, "Dr. Erez Yaary");
	CHECK_STR(c->org, "Atari Club");
	CHECK_STR(c->dept, "Hardware");
	CHECK_STR(c->title, "Chief Tinkerer");
	CHECK_STR(c->uid, "7B5D2E1A-0C3F-4A8B-9E21-55AA77CC1234");
	CHECK_EQ(c->nemail, 2);
	CHECK_STR(c->email[0].label, "home");
	CHECK_STR(c->email[0].value, "erez@example.com");
	CHECK_STR(c->email[1].label, "club");
	CHECK_EQ(c->ntel, 3);
	CHECK_STR(c->tel[0].label, "mobile");
	CHECK_STR(c->tel[1].label, "iPhone");
	CHECK_STR(c->tel[2].label, "home fax");
	CHECK_STR(c->tel[2].value, "03-555 1234");
	CHECK_EQ(c->nadr, 1);
	CHECK_STR(c->adr[0].label, "home");
	CHECK_STR(c->adr[0].value, "1 Herzl St, Apt 4\nTel Aviv\n\n6100000\nIsrael");
	CHECK_EQ(c->nurl, 1);
	CHECK_STR(c->url[0].label, "homepage");
	CHECK_STR(c->bday, "1975-05-17");
	CHECK_STR(c->note, "Met at the Atari meet, 1989.\nLikes Falcons.");
	CHECK(c->photo);
	CHECK(!c->group && !c->company);
	vcard_display_name(c, 0, s, sizeof(s));
	CHECK_STR(s, "Erez Yaary");
	vcard_display_name(c, 1, s, sizeof(s));
	CHECK_STR(s, "Yaary, Erez");
	vcard_initials(c, s);
	CHECK_STR(s, "EY");
	CHECK_STR(c->sortkey, "YAARY\001EREZ");
	vcard_bday_format(c->bday, s, sizeof(s));
	CHECK_STR(s, "17 May 1975");
	free(c);
}

static void test_roundtrip(void)
{
	CONTACT *c = vcard_parse(icloud_card, (long)strlen(icloud_card)), *d;
	CEDIT e;
	SBUF out;
	if (!c)
		return;
	vcard_to_edit(c, &e);
	CHECK_STR(e.bday, "17.05.1975");
	CHECK_STR(e.adr[0].street, "1 Herzl St, Apt 4");
	CHECK_STR(e.adr[0].city, "Tel Aviv");
	CHECK_STR(e.adr[0].country, "Israel");
	/* a change: a new phone, a work email, another title */
	str_copy(e.title, "Chief Engineer", sizeof(e.title));
	str_copy(e.tel[3].label, "work", sizeof(e.tel[3].label));
	str_copy(e.tel[3].value, "+972 3 000 0000", sizeof(e.tel[3].value));
	str_copy(e.email[1].value, "", sizeof(e.email[1].value));	/* the club address goes */
	sb_init(&out);
	CHECK(vcard_write(&e, c->uid, icloud_card, (long)strlen(icloud_card), &out));
	CHECK(strstr(out.s, "TITLE:Chief Engineer\r\n") != 0);
	CHECK(strstr(out.s, "TEL;TYPE=WORK;TYPE=VOICE:+972 3 000 0000\r\n") != 0);
	CHECK(strstr(out.s, "club.example.org") == 0);
	CHECK(strstr(out.s, ":club\r\n") == 0);			/* its label went with it */
	CHECK(strstr(out.s, "item5.X-ABRELATEDNAMES:Dana\r\n") != 0);	/* kept */
	CHECK(strstr(out.s, "item5.X-ABLabel:_$!<Spouse>!$_\r\n") != 0);
	CHECK(strstr(out.s, "PHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQ") != 0);
	CHECK(strstr(out.s, "X-SOCIALPROFILE;type=twitter:x-apple:erezyaary\r\n") != 0);
	CHECK(strstr(out.s, "UID:7B5D2E1A-0C3F-4A8B-9E21-55AA77CC1234\r\n") != 0);
	CHECK(strstr(out.s, "NOTE:Met at the Atari meet\\, 1989.\\nLikes Falcons.\r\n") != 0);
	CHECK(strstr(out.s, "BDAY;VALUE=date:1975-05-17\r\n") != 0);
	CHECK(strstr(out.s, "ORG:Atari Club;Hardware\r\n") != 0);
	CHECK(strstr(out.s, "N:Yaary;Erez;;Dr.;\r\n") != 0);
	CHECK(strstr(out.s, "FN:Dr. Erez Yaary\r\n") != 0);
	CHECK(strstr(out.s, "item2.X-ABLabel:_$!<HomeFAX>!$_") == 0);	/* home fax is a TYPE now */
	CHECK(strstr(out.s, "TEL;TYPE=HOME;TYPE=FAX:03-555 1234") != 0);
	/* the homepage is Apple's labelled item, numbered after the kept item5 */
	CHECK(strstr(out.s, "item6.URL:https://example.com/erez\r\n") != 0);
	CHECK(strstr(out.s, "item6.X-ABLabel:_$!<HomePage>!$_\r\n") != 0);
	/* no line longer than 75 bytes */
	{
		const char *p = out.s;
		int longest = 0;
		while (*p) {
			const char *e2 = strstr(p, "\r\n");
			if (!e2)
				break;
			if (e2 - p > longest)
				longest = (int)(e2 - p);
			p = e2 + 2;
		}
		CHECK(longest <= 75);
	}
	d = vcard_parse(out.s, out.len);
	CHECK(d != 0);
	if (d) {
		CHECK_STR(d->title, "Chief Engineer");
		CHECK_EQ(d->ntel, 4);
		CHECK_EQ(d->nemail, 1);
		CHECK_STR(d->url[0].label, "homepage");
		CHECK_STR(d->adr[0].value, "1 Herzl St, Apt 4\nTel Aviv\n\n6100000\nIsrael");
		CHECK(d->photo);
		free(d);
	}
	sb_free(&out);
	free(c);
}

static void test_hebrew_and_custom(void)
{
	/* Atari text in, UTF-8 out and back */
	CEDIT e;
	SBUF out;
	CONTACT *c;
	char *heb;
	long n;
	memset(&e, 0, sizeof(e));
	heb = cs_to_atari("\xd7\x93\xd7\xa0\xd7\x94", 6, CS_UTF8, &n);	/* Dana in Hebrew */
	str_copy(e.given, heb, sizeof(e.given));
	str_copy(e.family, "Levi", sizeof(e.family));
	str_copy(e.tel[0].label, "kibbutz", sizeof(e.tel[0].label));
	str_copy(e.tel[0].value, "04-1234567", sizeof(e.tel[0].value));
	str_copy(e.email[0].label, "work", sizeof(e.email[0].label));
	str_copy(e.email[0].value, "dana@example.org", sizeof(e.email[0].value));
	str_copy(e.bday, "3.11", sizeof(e.bday));
	sb_init(&out);
	CHECK(vcard_write(&e, "ABC-1", 0, 0, &out));
	CHECK(strstr(out.s, "N:Levi;\xd7\x93\xd7\xa0\xd7\x94;;;\r\n") != 0);
	CHECK(strstr(out.s, "item1.TEL:04-1234567\r\nitem1.X-ABLabel:kibbutz\r\n") != 0);
	CHECK(strstr(out.s, "EMAIL;TYPE=INTERNET;TYPE=WORK:dana@example.org\r\n") != 0);
	CHECK(strstr(out.s, "BDAY;X-APPLE-OMIT-YEAR=1604;VALUE=date:1604-11-03\r\n") != 0);
	CHECK(strstr(out.s, "UID:ABC-1\r\n") != 0);
	CHECK(strstr(out.s, "VERSION:3.0\r\n") != 0);
	c = vcard_parse(out.s, out.len);
	CHECK(c != 0);
	if (c) {
		CHECK_STR(c->given, heb);
		CHECK_STR(c->tel[0].label, "kibbutz");
		CHECK_STR(c->bday, "--11-03");
		free(c);
	}
	free(heb);
	sb_free(&out);
	/* nothing at all: refused */
	memset(&e, 0, sizeof(e));
	sb_init(&out);
	CHECK(!vcard_write(&e, "X", 0, 0, &out));
	sb_free(&out);
}

static void test_v4_and_21(void)
{
	static const char v4[] =
		"BEGIN:VCARD\nVERSION:4.0\nKIND:org\nFN:Atari Corp\nORG:Atari Corp\n"
		"TEL;VALUE=uri;TYPE=\"work,voice\":tel:+1-408-555-0100\nEMAIL:info@atari.example\n"
		"BDAY:--0417\nUID:urn:uuid:4fbe8971-0bc3-424c-9c26-36c3e1eff6b1\nEND:VCARD\n";
	static const char v21[] =
		"BEGIN:VCARD\r\nVERSION:2.1\r\nN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:=4D=C3=BC=6C=6C=65=72;J=C3=\r\n"
		"=BCrgen\r\nTEL;CELL;PREF:0170 1234567\r\nTEL;HOME:030 123456\r\n"
		"ADR;WORK;ENCODING=QUOTED-PRINTABLE:;;Hauptstra=C3=9Fe 1;Berlin;;10115;Deutschland\r\nEND:VCARD\r\n";
	static const char grp[] =
		"BEGIN:VCARD\r\nVERSION:3.0\r\nN:Atari Club\r\nFN:Atari Club\r\nX-ADDRESSBOOKSERVER-KIND:group\r\n"
		"X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:AAA\r\nX-ADDRESSBOOKSERVER-MEMBER:urn:uuid:BBB\r\n"
		"UID:GRP-1\r\nEND:VCARD\r\n";
	CONTACT *c = vcard_parse(v4, (long)strlen(v4));
	char s[80];
	CHECK(c != 0);
	if (c) {
		CHECK(c->company);
		CHECK_STR(c->uid, "4fbe8971-0bc3-424c-9c26-36c3e1eff6b1");
		CHECK_EQ(c->ntel, 1);
		CHECK_STR(c->tel[0].value, "+1-408-555-0100");
		CHECK_STR(c->tel[0].label, "work");
		CHECK_STR(c->email[0].label, "other");
		CHECK_STR(c->bday, "--04-17");
		vcard_display_name(c, 0, s, sizeof(s));
		CHECK_STR(s, "Atari Corp");
		CHECK_STR(c->sortkey, "ATARI CORP");
		vcard_bday_format(c->bday, s, sizeof(s));
		CHECK_STR(s, "17 April");
		free(c);
	}
	c = vcard_parse(v21, (long)strlen(v21));
	CHECK(c != 0);
	if (c) {
		/* "Müller", "Jürgen": u-umlaut is 0x81 on the Atari */
		CHECK_STR(c->family, "M\x81ller");
		CHECK_STR(c->given, "J\x81rgen");
		CHECK_STR(c->sortkey, "MULLER\001JURGEN");
		CHECK_EQ(c->ntel, 2);
		CHECK_STR(c->tel[0].label, "mobile");
		CHECK_STR(c->tel[1].label, "home");
		CHECK_STR(c->adr[0].label, "work");
		CHECK_STR(c->adr[0].value, "Hauptstra\x9e" "e 1\nBerlin\n\n10115\nDeutschland");
		free(c);
	}
	c = vcard_parse(grp, (long)strlen(grp));
	CHECK(c != 0);
	if (c) {
		SBUF out;
		const char *m[3];
		CHECK(c->group);
		CHECK_EQ(c->nmember, 2);
		CHECK_STR(c->member[1], "BBB");
		m[0] = c->member[0];
		m[1] = c->member[1];
		m[2] = "CCC";
		sb_init(&out);
		CHECK(vcard_write_group("Atari Friends", c->uid, m, 3, grp, (long)strlen(grp), &out));
		CHECK(strstr(out.s, "FN:Atari Friends\r\n") != 0);
		CHECK(strstr(out.s, "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:CCC\r\n") != 0);
		CHECK(strstr(out.s, "UID:GRP-1\r\n") != 0);
		CHECK(strstr(out.s, "Atari Club") == 0);
		sb_free(&out);
		free(c);
	}
	{
		/* categories, Nextcloud's groups */
		static const char nc[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Sam\r\nN:;Sam;;;\r\n"
					 "CATEGORIES:Family,Atari\\, Club\r\nUID:S1\r\nEND:VCARD\r\n";
		SBUF out;
		c = vcard_parse(nc, (long)strlen(nc));
		CHECK(c && c->cats && !strcmp(c->cats, "Family,Atari, Club"));
		free(c);
		sb_init(&out);
		CHECK(vcard_set_categories(nc, (long)strlen(nc), "Family,Work", &out));
		CHECK(strstr(out.s, "CATEGORIES:Family,Work\r\n") != 0);
		CHECK(strstr(out.s, "UID:S1\r\n") != 0);
		sb_free(&out);
	}
}

static int count_cb(CONTACT *c, void *ud)
{
	(*(int *)ud)++;
	free(c);
	return 1;
}

static void test_many(void)
{
	static const char two[] = "BEGIN:VCARD\nVERSION:3.0\nFN:A\nEND:VCARD\n"
				  "BEGIN:VCARD\nVERSION:3.0\nFN:B\nEND:VCARD\n";
	int n = 0;
	CHECK_EQ(vcard_parse_all(two, (long)strlen(two), count_cb, &n), 2);
	CHECK_EQ(n, 2);
}

static void test_bday(void)
{
	char s[20];
	int y, m, d;
	CHECK(vcard_bday_parse("17.05.1975", s, sizeof(s)) && !strcmp(s, "1975-05-17"));
	CHECK(vcard_bday_parse("1.2.85", s, sizeof(s)) && !strcmp(s, "1985-02-01"));
	CHECK(vcard_bday_parse("17.5", s, sizeof(s)) && !strcmp(s, "--05-17"));
	CHECK(vcard_bday_parse("1975-05-17", s, sizeof(s)) && !strcmp(s, "1975-05-17"));
	CHECK(!vcard_bday_parse("32.1.2000", s, sizeof(s)));
	CHECK(!vcard_bday_parse("hello", s, sizeof(s)));
	CHECK(vcard_bday_split("19750517", &y, &m, &d) && y == 1975 && m == 5 && d == 17);
	CHECK(vcard_bday_split("--0517", &y, &m, &d) && y == 0 && m == 5 && d == 17);
	CHECK(vcard_bday_split("1975-05-17T00:00:00Z", &y, &m, &d) && y == 1975);
}

static void test_urls(void)
{
	URL u;
	char s[200];
	CHECK(url_parse("https://contacts.icloud.com/", &u));
	CHECK(u.tls && u.port == 443 && !strcmp(u.host, "contacts.icloud.com"));
	url_resolve("https://p42-contacts.icloud.com:443/123/carddavhome/", "/123/carddavhome/card/", s, sizeof(s));
	CHECK_STR(s, "https://p42-contacts.icloud.com/123/carddavhome/card/");
}

/* ---- photos ---- */

/* the colour of a pixel, 8 bits each */
static void rgb_at(const PHOTOIMG *im, int x, int y, int *r, int *g, int *b)
{
	unsigned short p = im->px[y * PHOTO_N + x];
	*r = (p >> 11) << 3;
	*g = ((p >> 5) & 63) << 2;
	*b = (p & 31) << 3;
}

static int near(const PHOTOIMG *im, int x, int y, int r, int g, int b)
{
	int pr, pg, pb;
	rgb_at(im, x, y, &pr, &pg, &pb);
	return abs(pr - r) < 40 && abs(pg - g) < 40 && abs(pb - b) < 40;
}

/* the four quarters where they belong: red, green / blue, yellow */
static int quarters_ok(const PHOTOIMG *im)
{
	return near(im, 8, 8, 230, 30, 30) && near(im, 55, 8, 30, 200, 40) && near(im, 8, 55, 30, 40, 220) &&
	       near(im, 55, 55, 240, 220, 20);
}

/* a vCard with the JPEG in it, as the Mac writes it: base64, folded */
static char *card_with(const unsigned char *jpg, long n, const char *params, int v4)
{
	long bl;
	char *b = base64_encode((const char *)jpg, n, 0, &bl);
	SBUF s;
	long i;
	sb_init(&s);
	sb_adds(&s, "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Pic\r\n");
	sb_printf(&s, v4 ? "PHOTO%s:data:image/jpeg;base64," : "PHOTO%s:", params);
	for (i = 0; i < bl; i += 60) {
		if (i)
			sb_adds(&s, "\r\n ");
		sb_add(&s, b + i, bl - i < 60 ? bl - i : 60);
	}
	sb_adds(&s, "\r\nUID:pic-1\r\nEND:VCARD\r\n");
	free(b);
	return sb_steal(&s);
}

static void test_photos(void)
{
	PHOTOIMG im;
	PHOTOREF ref;
	char *card;
	CONTACT *c, *c2;
	memset(&ref, 0, sizeof(ref));
	CHECK(photo_decode_jpeg(jpg_wide, sizeof(jpg_wide), 0, &im));
	CHECK(quarters_ok(&im));
	CHECK(photo_decode_jpeg(jpg_square, sizeof(jpg_square), 0, &im));
	CHECK(quarters_ok(&im));
	CHECK(photo_decode_jpeg(jpg_small, sizeof(jpg_small), 0, &im));
	CHECK(quarters_ok(&im));
	CHECK(photo_decode_jpeg(jpg_gray, sizeof(jpg_gray), 0, &im));
	CHECK(near(&im, 30, 30, 128, 128, 128));
	CHECK(!photo_decode_jpeg(jpg_prog, sizeof(jpg_prog), 0, &im));
	CHECK(!photo_decode_jpeg((const unsigned char *)"not a picture", 13, 0, &im));
	/* Apple's crop, its y from the bottom: the bottom right quarter */
	ref.has_crop = 1;
	ref.crop[0] = 256;
	ref.crop[1] = 0;
	ref.crop[2] = 256;
	ref.crop[3] = 256;
	CHECK(photo_decode_jpeg(jpg_square, sizeof(jpg_square), &ref, &im));
	CHECK(near(&im, 4, 4, 240, 220, 20) && near(&im, 60, 60, 240, 220, 20));

	/* the PHOTO line: 3.0 as the Mac writes it, with Apple's crop */
	card = card_with(jpg_square, sizeof(jpg_square),
			 ";X-ABCROP-RECTANGLE=ABClipRect_1&0&256&256&256&Rk9P;ENCODING=b;TYPE=JPEG", 0);
	c = vcard_parse(card, (long)strlen(card));
	CHECK(c && c->photo && c->photo_key);
	CHECK(photo_ref(card, (long)strlen(card), &ref));
	CHECK_EQ(ref.kind, PR_DATA);
	CHECK_EQ(ref.fmt, PF_JPEG);
	CHECK_EQ(ref.len, (long)sizeof(jpg_square));
	CHECK(ref.data && !memcmp(ref.data, jpg_square, sizeof(jpg_square)));
	CHECK(ref.has_crop && ref.crop[0] == 0 && ref.crop[1] == 256 && ref.crop[2] == 256);
	CHECK(photo_decode_jpeg(ref.data, ref.len, &ref, &im));
	CHECK(near(&im, 30, 30, 230, 30, 30));		/* the top left quarter */
	free(ref.data);
	free(card);
	/* another photo, another fingerprint; the same one, the same */
	card = card_with(jpg_wide, sizeof(jpg_wide), ";ENCODING=b;TYPE=JPEG", 0);
	c2 = vcard_parse(card, (long)strlen(card));
	CHECK(c2 && c2->photo_key && c2->photo_key != c->photo_key);
	free(c2);
	c2 = vcard_parse(card, (long)strlen(card));
	{
		CONTACT *c3 = vcard_parse(card, (long)strlen(card));
		CHECK(c3 && c2 && c3->photo_key == c2->photo_key);
		free(c3);
	}
	free(c2);
	free(c);
	free(card);
	/* 4.0's data: address */
	card = card_with(jpg_small, sizeof(jpg_small), "", 1);
	CHECK(photo_ref(card, (long)strlen(card), &ref) && ref.kind == PR_DATA && ref.len == (long)sizeof(jpg_small));
	free(ref.data);
	free(card);
	/* links */
	{
		static const char a[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:L\r\nPHOTO;VALUE=uri:https://p42-contacts.icloud.com/\r\n"
					" 1234/wbs/abcdef\r\nEND:VCARD\r\n";
		static const char b[] = "BEGIN:VCARD\nVERSION:4.0\nFN:L\nPHOTO;MEDIATYPE=image/png:http://x.example/me.png\nEND:VCARD\n";
		static const char n[] = "BEGIN:VCARD\nVERSION:3.0\nFN:No\nEND:VCARD\n";
		char t[300];
		strcpy(t, a);
		CHECK(photo_ref(t, (long)strlen(t), &ref) && ref.kind == PR_URI);
		CHECK_STR(ref.uri, "https://p42-contacts.icloud.com/1234/wbs/abcdef");
		strcpy(t, b);
		CHECK(photo_ref(t, (long)strlen(t), &ref) && ref.kind == PR_URI);
		CHECK_STR(ref.uri, "http://x.example/me.png");
		strcpy(t, n);
		CHECK(!photo_ref(t, (long)strlen(t), &ref));
		c = vcard_parse(n, (long)strlen(n));
		CHECK(c && !c->photo_key);
		free(c);
	}
	/* 2.1: base64 lines without the space, to a blank line */
	{
		long bl;
		char *b64 = base64_encode((const char *)jpg_small, sizeof(jpg_small), 76, &bl);
		SBUF s;
		sb_init(&s);
		/* (sb_printf keeps to 512 characters: the base64 goes in whole) */
		sb_adds(&s, "BEGIN:VCARD\r\nVERSION:2.1\r\nN:Old;Phone\r\nPHOTO;JPEG;ENCODING=BASE64:\r\n");
		sb_adds(&s, b64);
		sb_adds(&s, "\r\n\r\nTEL;CELL:123\r\nEND:VCARD\r\n");
		CHECK(photo_ref(s.s, s.len, &ref) && ref.kind == PR_DATA && ref.len == (long)sizeof(jpg_small) &&
		      ref.fmt == PF_JPEG);
		free(ref.data);
		sb_free(&s);
		free(b64);
	}
}


/* the photo apart from the card: where its line is, its fingerprint,
   and the cache's stand-in */
static void test_photo_split(void)
{
	static const char folded[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:F\r\nPHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQ\r\n"
				     " SkZJRgABAQ\r\n AAAQABAAD\r\nEMAIL:f@x.org\r\nEND:VCARD\r\n";
	static const char refolded[] = "BEGIN:VCARD\nVERSION:3.0\nFN:F\nitem1.PHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQSkZJ\n"
				       "  RgABAQAAAQ\n ABAAD\nEMAIL:f@x.org\nEND:VCARD\n";
	static const char other[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:F\r\nPHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQ\r\n"
				    " SkZJRgABAQ\r\n AAAQABAAE\r\nEMAIL:f@x.org\r\nEND:VCARD\r\n";
	static const char v21[] = "BEGIN:VCARD\r\nVERSION:2.1\r\nN:A;B\r\nPHOTO;JPEG;ENCODING=BASE64:\r\n"
				  "/9j/4AAQSkZJRgAB\r\nAQAAAQABAAD\r\n\r\nEMAIL:b@x.org\r\nEND:VCARD\r\n";
	static const char novalue[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:N\r\nPHOTO;ENCODING=b;TYPE=JPEG:\r\nEND:VCARD\r\n";
	static const char marked[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:M\r\nX-ECONTACT-PHOTO:1A2B3C4D\r\nEND:VCARD\r\n";
	static const char unknown[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:U\r\nX-ECONTACT-PHOTO:?\r\nEND:VCARD\r\n";
	long s, e, s2, e2;
	CONTACT *c;
	CHECK(vcard_photo_span(folded, (long)strlen(folded), &s, &e));
	CHECK(!strncmp(folded + s, "PHOTO;", 6) && !strncmp(folded + e, "EMAIL:", 6));
	CHECK(!vcard_photo_empty(folded + s, e - s));
	/* folded another way, the same photo */
	CHECK(vcard_photo_span(refolded, (long)strlen(refolded), &s2, &e2));
	CHECK(!strncmp(refolded + s2, "item1.PHOTO", 11) && !strncmp(refolded + e2, "EMAIL:", 6));
	CHECK(vcard_photo_key(folded + s, e - s) == vcard_photo_key(refolded + s2 + 6, e2 - s2 - 6));
	/* one letter different, another key */
	CHECK(vcard_photo_span(other, (long)strlen(other), &s2, &e2));
	CHECK(vcard_photo_key(folded + s, e - s) != vcard_photo_key(other + s2, e2 - s2));
	/* the parser's key is the same as the cache's */
	c = vcard_parse(folded, (long)strlen(folded));
	CHECK(c && c->photo_key == vcard_photo_key(folded + s, e - s));
	CHECK(c && c->photo_key > VC_KEY_UNKNOWN);
	free(c);
	/* 2.1's base64 runs on to the blank line */
	CHECK(vcard_photo_span(v21, (long)strlen(v21), &s, &e));
	CHECK(!strncmp(v21 + e, "\r\nEMAIL", 7) || !strncmp(v21 + e, "EMAIL", 5));
	/* asked for without its value */
	CHECK(vcard_photo_span(novalue, (long)strlen(novalue), &s, &e));
	CHECK(vcard_photo_empty(novalue + s, e - s));
	c = vcard_parse(novalue, (long)strlen(novalue));
	CHECK(c && c->photo && c->photo_key == VC_KEY_UNKNOWN);
	free(c);
	/* the stand-ins */
	c = vcard_parse(marked, (long)strlen(marked));
	CHECK(c && c->photo && c->photo_key == 0x1A2B3C4DUL);
	free(c);
	c = vcard_parse(unknown, (long)strlen(unknown));
	CHECK(c && c->photo && c->photo_key == VC_KEY_UNKNOWN);
	free(c);
	CHECK(!vcard_photo_span(marked, (long)strlen(marked), &s, &e));
}

int main(void)
{
	test_parse_icloud();
	test_roundtrip();
	test_hebrew_and_custom();
	test_v4_and_21();
	test_many();
	test_bday();
	test_urls();
	test_photos();
	test_photo_split();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
