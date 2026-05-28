#pragma once

/**
 * privileged_ops_run:
 * @argc: عدد وسائط ما بعد "--privileged"
 * @argv: مصفوفة الوسائط ابتداءً من "--privileged"
 *
 * نقطة دخول وضع المساعد المحمي (أمر واحد ثم خروج).
 * يُستدعى من main() عندما يكون argv[1] == "--privileged".
 */
int privileged_ops_run(int argc, char **argv);

/**
 * privileged_ops_run_daemon:
 *
 * وضع الـ daemon — يقرأ أوامراً من stdin سطراً بسطر ويردّ على stdout.
 * يبقى حياً حتى يُغلق stdin (EOF) أو يُرسَل "quit".
 * يُستدعى من main() عندما يكون argv[1] == "--privileged-daemon".
 */
int privileged_ops_run_daemon(void);
