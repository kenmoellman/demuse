# Legacy text file indexing - used by @text command only.
# Help and news are now stored in MariaDB.
# To import legacy help/news into MariaDB, use @help/import and @news/import.

all: helpindx newsindx

helpindx: helptext
	../../bin/mkindx help

newsindx: newstext
	../../bin/mkindx news
