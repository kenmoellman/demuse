#!/usr/bin/env python3
"""Generate SQL INSERT statements from legacy helptext/newstext files.

Parses the old-format & topic_name markers and outputs SQL suitable for
appending to config/defaults.sql.

Handles alias patterns: when a topic has an empty body and is immediately
followed by another topic, both get the next topic's body content.
"""

import sys
import os


def parse_topics(filename):
    """Parse legacy helptext file into list of (name, body) tuples."""
    topics = []
    current_name = None
    current_body = []

    with open(filename, 'r') as f:
        for line in f:
            if line.startswith('& '):
                if current_name is not None:
                    body = ''.join(current_body).rstrip('\n\r')
                    topics.append((current_name, body))
                current_name = line[2:].strip()
                current_body = []
            elif current_name is not None:
                current_body.append(line)

    if current_name is not None:
        body = ''.join(current_body).rstrip('\n\r')
        topics.append((current_name, body))

    return topics


def resolve_aliases(topics):
    """Resolve empty-body alias topics by copying the next topic's body."""
    resolved = []
    for i, (name, body) in enumerate(topics):
        if not body and i + 1 < len(topics):
            # Alias: use next topic's body
            resolved.append((name, topics[i + 1][1]))
        else:
            resolved.append((name, body))
    return resolved


def sql_escape(s):
    """Escape a string for SQL single-quoted literal."""
    return s.replace("'", "''")


def generate_help_sql(topics):
    """Generate INSERT statements for help_topics table."""
    lines = []
    lines.append("-- ============================================================================")
    lines.append("-- DEFAULT HELP TOPICS")
    lines.append("-- ============================================================================")
    lines.append("-- Imported from legacy helptext file. All stored as command=topic, subcommand=''.")
    lines.append("-- Uses INSERT IGNORE so existing customized topics are preserved.")
    lines.append("")

    for name, body in topics:
        if not body:
            continue
        esc_name = sql_escape(name)
        esc_body = sql_escape(body)
        lines.append(
            f"INSERT IGNORE INTO help_topics (command, subcommand, body) "
            f"VALUES ('{esc_name}', '', '{esc_body}');"
        )

    return '\n'.join(lines)


def generate_news_sql():
    """Generate a default welcome news article."""
    lines = []
    lines.append("")
    lines.append("-- ============================================================================")
    lines.append("-- DEFAULT NEWS ARTICLE")
    lines.append("-- ============================================================================")
    lines.append("-- A generic welcome article. Author is dbref 1 (root).")
    lines.append("")

    body = sql_escape(
        "Welcome to deMUSE!\n"
        "\n"
        "This is a modernized version of the classic TinyMUSE server, descended from\n"
        "TinyMUSE '97 and TinyMUSE 1.8a4.\n"
        "\n"
        "Type 'help' for the online help system.\n"
        "Type '+news list' to see all news articles.\n"
        "Type 'who' to see who else is online.\n"
        "\n"
        "Enjoy your stay!"
    )

    lines.append(
        f"INSERT IGNORE INTO news (news_id, topic, body, author) "
        f"VALUES (1, 'Welcome to deMUSE', '{body}', 1);"
    )

    return '\n'.join(lines)


def main():
    helptext = os.path.join(os.path.dirname(__file__), '..', 'run', 'msgs', 'helptext')

    topics = parse_topics(helptext)
    topics = resolve_aliases(topics)

    print(f"Parsed {len(topics)} help topics ({sum(1 for _, b in topics if b)} with content)")

    help_sql = generate_help_sql(topics)
    news_sql = generate_news_sql()

    output = help_sql + '\n' + news_sql + '\n'

    outfile = os.path.join(os.path.dirname(__file__), '..', 'config', 'help_seed.sql')
    with open(outfile, 'w') as f:
        f.write(output)

    print(f"Written to {outfile}")


if __name__ == '__main__':
    main()
