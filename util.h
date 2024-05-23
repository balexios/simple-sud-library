#pragma once

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <cstdint>

#define ASSERT_ELSE_PERROR(cond)	\
	do {							\
		bool x = static_cast<bool>(cond);	\
		if (!x) {							\
			fflush(stdout);					\
			fflush(stderr);					\
			fprintf(stderr,"%s:%d: %s: Assertion `%s` failed: ", __FILE__, __LINE__, __func__, #cond);	\
			perror("");						\
 			fflush(stderr);					\
			abort();						\
		}									\
	} while (false)
