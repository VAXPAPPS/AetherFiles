#pragma once

/**
 * privileged_ops_run:
 * @argc: عدد وسائط ما بعد "--privileged"
 * @argv: مصفوفة الوسائط ابتداءً من "--privileged"
 *
 * نقطة دخول وضع المساعد المحمي.
 * يُستدعى من main() عندما يكون argv[1] == "--privileged".
 * يُنفّذ العملية ويُخرج النتيجة على stdout ثم يُعيد كود الخروج.
 */
int privileged_ops_run(int argc, char **argv);
