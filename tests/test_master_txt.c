#include "test_util.h"
#include "openbc/master.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * masterserver.txt parser tests (#201)
 *
 * bc_master_parse_txt() reads a masterserver.txt-style file (one "host:port"
 * per line, '#' comments, blank lines skipped) into a caller buffer. These
 * tests exercise the parser purely from disk -- no DNS, no sockets.
 * ---------------------------------------------------------------------- */

/* Write text to a temp file; returns the path (static buffer). */
static const char *write_temp(const char *content)
{
    static char path[] = "obc_master_txt_test.tmp";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return path;
}

static void remove_temp(const char *path)
{
    if (path) remove(path);
}

TEST(parse_simple_two_entries)
{
    const char *path = write_temp(
        "masterserver.example.com:27900\n"
        "backup.example.com:27900\n");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT_EQ_INT(n, 2);
    ASSERT(strcmp(out[0], "masterserver.example.com:27900") == 0);
    ASSERT(strcmp(out[1], "backup.example.com:27900") == 0);
}

TEST(comments_and_blanks_skipped)
{
    const char *path = write_temp(
        "# this is a comment\n"
        "\n"
        "   \t  \n"
        "  # indented comment\n"
        "master.example.com:27900\n"
        "\n"
        "# trailing comment\n");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT_EQ_INT(n, 1);
    ASSERT(strcmp(out[0], "master.example.com:27900") == 0);
}

TEST(whitespace_and_crlf_trimmed)
{
    /* CRLF line endings + surrounding whitespace must be trimmed. */
    const char *path = write_temp(
        "   master.example.com:27900   \r\n"
        "\tbackup.example.com:28900\t\r\n");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT_EQ_INT(n, 2);
    ASSERT(strcmp(out[0], "master.example.com:27900") == 0);
    ASSERT(strcmp(out[1], "backup.example.com:28900") == 0);
}

TEST(inline_trailing_comment_stripped)
{
    const char *path = write_temp(
        "master.example.com:27900  # primary master\n");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT_EQ_INT(n, 1);
    ASSERT(strcmp(out[0], "master.example.com:27900") == 0);
}

TEST(first_line_wins_ordering)
{
    /* Older "first non-comment line wins" consumers read out[0]. */
    const char *path = write_temp(
        "# header\n"
        "primary.example.com:27900\n"
        "secondary.example.com:27900\n");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT(n >= 1);
    ASSERT(strcmp(out[0], "primary.example.com:27900") == 0);
}

TEST(missing_file_returns_negative)
{
    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt("this_file_does_not_exist_obc.tmp",
                                out, BC_MASTERSERVER_TXT_MAX);
    ASSERT(n < 0);
}

TEST(respects_max_out_cap)
{
    const char *path = write_temp(
        "a.example.com:27900\n"
        "b.example.com:27900\n"
        "c.example.com:27900\n");
    ASSERT(path != NULL);

    char out[2][128];
    int n = bc_master_parse_txt(path, out, 2);
    remove_temp(path);

    ASSERT_EQ_INT(n, 2);
    ASSERT(strcmp(out[0], "a.example.com:27900") == 0);
    ASSERT(strcmp(out[1], "b.example.com:27900") == 0);
}

TEST(empty_file_returns_zero)
{
    const char *path = write_temp("");
    ASSERT(path != NULL);

    char out[BC_MASTERSERVER_TXT_MAX][128];
    int n = bc_master_parse_txt(path, out, BC_MASTERSERVER_TXT_MAX);
    remove_temp(path);

    ASSERT_EQ_INT(n, 0);
}

TEST_MAIN_BEGIN()
    RUN(parse_simple_two_entries);
    RUN(comments_and_blanks_skipped);
    RUN(whitespace_and_crlf_trimmed);
    RUN(inline_trailing_comment_stripped);
    RUN(first_line_wins_ordering);
    RUN(missing_file_returns_negative);
    RUN(respects_max_out_cap);
    RUN(empty_file_returns_zero);
TEST_MAIN_END()
