#include "Tokenizer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// An empty config file must produce zero tokens. This is the minimum baseline
// behavior of the tokenizer.
TEST(TokenizerTest, EmptyContent) {
  std::string input = "";
  webserv::Tokenizer tkner(input);

  EXPECT_EQ(tkner.getTokens().size(), 0u);
  EXPECT_TRUE(tkner.getTokens().empty());
}
// A file consisting entirely of comment lines must be stripped to nothing,
// producing zero tokens.
TEST(TokenizerTest, CommentsOnly) {
  std::string input = "# server config"
                      "#nothing to see here"
                      "#another comment ";
  webserv::Tokenizer tkner(input);

  EXPECT_EQ(tkner.getTokens().size(), 0u);
  EXPECT_TRUE(tkner.getTokens().empty());
}
// A bare word with no delimiters must produce exactly one Word token.
TEST(TokenizerTest, SingleWordToken) {
  std::string input = "server";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
}
// The tokenizer must recognize { as a structural delimiter and emit it as its
// own token even without surrounding whitespace.
TEST(TokenizerTest, SingleOpenBracket) {
  std::string input = "{";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[0].value, "{");
}
// } must be tokenized as a CloseBracket token.
TEST(TokenizerTest, SingleCloseBracket) {
  std::string input = "}";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::CloseBracket);
  EXPECT_EQ(tokens[0].value, "}");
}
// The semicolon is a directive terminator and must be recognized as its own
// token.
TEST(TokenizerTest, SingleSemicolon) {
  std::string input = ";";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[0].value, ";");
}
// A delimiter directly attached to a word must split off as a separate token
// -- no whitespace required between them.
TEST(TokenizerTest, WordFollowedBySemicolonNoSpace) {
  std::string input = "8080;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "8080");
  EXPECT_EQ(tokens[1].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[1].value, ";");
}
// server{ must split into a word and an opening brace token without requiring
// a space between them.
TEST(TokenizerTest, WordDirectlyFollowedByOpenBracket) {
  std::string input = "server{";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");
}
// Runs of multiple spaces must collapse to a single delimiter -- no empty
// intermediate tokens are emitted.
TEST(TokenizerTest, MultipleSpacesBetweenWords) {
  std::string input = "listen     8080";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
  EXPECT_EQ(tokens[1].tokenType, webserv::Word);
  EXPECT_EQ(tokens[1].value, "8080");
}
// Leading and trailing whitespace must not produce empty tokens at either end
// of the stream.
TEST(TokenizerTest, LeadingAndTrailingSpaces) {
  std::string input = "   listen   ";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
}
// Consecutive delimiters do not collapse -- each must be emitted as its own
// token.
TEST(TokenizerTest, TwoConsecutiveSemicolons) {
  std::string input = ";;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[0].value, ";");
  EXPECT_EQ(tokens[1].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[1].value, ";");
}
// Adjacent structural delimiters of different kinds must each become their own
// token.
TEST(TokenizerTest, TwoConsecutiveBraces) {
  std::string input = "{}";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[0].value, "{");
  EXPECT_EQ(tokens[1].tokenType, webserv::CloseBracket);
  EXPECT_EQ(tokens[1].value, "}");
}
// A typical directive must produce three tokens in the order shown, regardless
// of how much whitespace separates the words.
TEST(TokenizerTest, SingleLineDirective) {
  std::string input = "listen 8080;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
  EXPECT_EQ(tokens[1].tokenType, webserv::Word);
  EXPECT_EQ(tokens[1].value, "8080");
  EXPECT_EQ(tokens[2].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[2].value, ";");
}
// A # strips everything from itself to the end of the line, including any
// delimiters that appear after it.
TEST(TokenizerTest, CommentAfterDirectiveOnSameLine) {
  std::string input = "listen 8080; # default port";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
  EXPECT_EQ(tokens[1].tokenType, webserv::Word);
  EXPECT_EQ(tokens[1].value, "8080");
  EXPECT_EQ(tokens[2].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[2].value, ";");
}
// Anything to the right of # is removed before tokenization, including
// structural delimiters that would otherwise have been recognized.
TEST(TokenizerTest, CommentSwallowsWouldBeTokens) {
  std::string input = "server { # } ;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");
}
// A line whose only non-whitespace content is a comment must contribute
// nothing to the token stream.
TEST(TokenizerTest, CommentAtStartOfLine) {
  std::string input = "# this entire line is a comment\n"
                      "listen 8080";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
  EXPECT_EQ(tokens[1].tokenType, webserv::Word);
  EXPECT_EQ(tokens[1].value, "8080");
}
// A small but complete server block exercises every recognized token kind in a
// realistic combination.
TEST(TokenizerTest, FullServerBlockWithAllDelimiterKinds) {
  std::string input = "server { listen 8080; }";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 6u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");
  EXPECT_EQ(tokens[2].tokenType, webserv::Word);
  EXPECT_EQ(tokens[2].value, "listen");
  EXPECT_EQ(tokens[3].tokenType, webserv::Word);
  EXPECT_EQ(tokens[3].value, "8080");
  EXPECT_EQ(tokens[4].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[4].value, ";");
  EXPECT_EQ(tokens[5].tokenType, webserv::CloseBracket);
  EXPECT_EQ(tokens[5].value, "}");
}
// The tokenizer must split on {}; even without any surrounding whitespace.
TEST(TokenizerTest, TightlyPackedConfigWithoutSpaces) {
  std::string input = "server{listen 8080;}";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 6u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");
  EXPECT_EQ(tokens[2].tokenType, webserv::Word);
  EXPECT_EQ(tokens[2].value, "listen");
  EXPECT_EQ(tokens[3].tokenType, webserv::Word);
  EXPECT_EQ(tokens[3].value, "8080");
  EXPECT_EQ(tokens[4].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[4].value, ";");
  EXPECT_EQ(tokens[5].tokenType, webserv::CloseBracket);
  EXPECT_EQ(tokens[5].value, "}");
}
// The tokenizer appends each line's stripped content to a single buffer with no
// delimiter, so words touching across a newline are merged.
TEST(TokenizerTest, AdjacentLinesWithNoWhitespaceConcatenate) {
  std::string input = "listen 127.0.0.1:8080\n"
                      "listen 0.0.0.0:8080;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_GE(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
  EXPECT_EQ(tokens[1].tokenType, webserv::Word);
  EXPECT_EQ(tokens[1].value, "127.0.0.1:8080listen");
  EXPECT_EQ(tokens[2].tokenType, webserv::Word);
  EXPECT_EQ(tokens[2].value, "0.0.0.0:8080");
  EXPECT_EQ(tokens[3].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[3].value, ";");
}
// Filesystem paths must be preserved verbatim as a single word -- / and . are
// not delimiters.
TEST(TokenizerTest, PathLikeWordWithSlashesAndDots) {
  std::string input = "./www/static/index.html";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "./www/static/index.html");
}
// The tokenizer does not interpret : -- a host:port pair stays as one word and
// is split later by the parser.
TEST(TokenizerTest, HostPortWord) {
  std::string input = "127.0.0.1:8080";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "127.0.0.1:8080");
}
// The tokenizer treats only space and {}; as delimiters; tabs are folded into
// the surrounding word.
TEST(TokenizerTest, TabCharactersAreNotADelimiter) {
  std::string input = "listen\t8080";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, std::string("listen\t8080"));
}
// readFile opens an ifstream without checking for failure; a non-existent file
// yields an empty buffer and therefore zero tokens.
TEST(TokenizerTest, MissingFile) {
  webserv::Tokenizer tkner;

  EXPECT_NO_THROW(tkner.readFile("fixtures/this_file_does_not_exist.conf"));
  EXPECT_TRUE(tkner.getTokens().empty());
}
// Each structural delimiter character is its own token even when the
// characters are different and pressed together with no whitespace.
TEST(TokenizerTest, MixedDelimitersInARow) {
  std::string input = ";{}";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Semicol);
  EXPECT_EQ(tokens[0].value, ";");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");
  EXPECT_EQ(tokens[2].tokenType, webserv::CloseBracket);
  EXPECT_EQ(tokens[2].value, "}");
}
// The tokenizer strips comments at the line level by searching for # in the
// raw line; it does not understand quoting or escaping.
TEST(TokenizerTest, HashInsideAWordStillStartsAComment) {
  std::string input = "listen #8080;";
  webserv::Tokenizer tkner(input);

  const auto &tokens = tkner.getTokens();

  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "listen");
}
// A smoke test using the existing valid_big.conf fixture confirms the tokenizer
// can handle a realistic multi-server configuration.
TEST(TokenizerTest, LargeValidConfigProducesWellFormedTokenStream) {
  webserv::Tokenizer tkner;
  tkner.readFile("fixtures/valid_big.conf");

  const auto &tokens = tkner.getTokens();

  ASSERT_FALSE(tokens.empty());
  ASSERT_GE(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].tokenType, webserv::Word);
  EXPECT_EQ(tokens[0].value, "server");
  EXPECT_EQ(tokens[1].tokenType, webserv::OpenBracket);
  EXPECT_EQ(tokens[1].value, "{");

  std::size_t opens = 0;
  std::size_t closes = 0;
  for (const auto &token : tokens) {
    if (token.tokenType == webserv::OpenBracket)
      ++opens;
    if (token.tokenType == webserv::CloseBracket)
      ++closes;
  }
  EXPECT_EQ(opens, closes);
  EXPECT_GT(opens, 0u);
}
// Undefined is reserved for the internal getDelimType helper and must never
// escape to the public token stream.
TEST(TokenizerTest, TokenTypeOfEveryEmittedTokenIsAValidEnumerator) {
  webserv::Tokenizer tkner;
  tkner.readFile("fixtures/valid_big.conf");

  for (const auto &token : tkner.getTokens()) {
    EXPECT_NE(token.tokenType, webserv::Undefined);
    EXPECT_TRUE(token.tokenType == webserv::Word ||
                token.tokenType == webserv::OpenBracket ||
                token.tokenType == webserv::CloseBracket ||
                token.tokenType == webserv::Semicol);
  }
}
