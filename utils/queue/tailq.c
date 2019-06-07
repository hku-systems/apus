#include <sys/queue.h>

struct entry {
	// element
	TAILQ_ENTRY(entry)	entries;
} *n1;

TAILQ_HEAD(, entry) head;

int main(int argc, char const *argv[])
{
	TAILQ_INIT(&head);

	n1	= malloc(sizeof(struct entry));

	TAILQ_INSERT_TAIL(&head, n1, entries);

	while (!TAILQ_EMPTY(&head)) {
		n1	= TAILQ_FIRST(&head);
		TAILQ_REMOVE(&head, n1, entries);
		free(n1);
	}

	return 0;
}

