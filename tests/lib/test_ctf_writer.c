/*
 * test-ctf-writer.c
 *
 * CTF Writer test
 *
 * Copyright 2013 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <babeltrace/ctf-writer/writer.h>
#include <babeltrace/ctf-writer/clock.h>
#include <babeltrace/ctf-writer/stream.h>
#include <babeltrace/ctf-writer/event.h>
#include <babeltrace/ctf-writer/event-types.h>
#include <babeltrace/ctf-writer/event-fields.h>
#include <babeltrace/ctf/events.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include "tap/tap.h"
#include <math.h>
#include <float.h>

#define METADATA_LINE_SIZE 512
#define SEQUENCE_TEST_LENGTH 10
#define PACKET_RESIZE_TEST_LENGTH 100000

#define DEFAULT_CLOCK_FREQ 1000000000
#define DEFAULT_CLOCK_PRECISION 1
#define DEFAULT_CLOCK_OFFSET 0
#define DEFAULT_CLOCK_OFFSET_S 0
#define DEFAULT_CLOCK_IS_ABSOLUTE 0
#define DEFAULT_CLOCK_TIME 0

static uint64_t current_time = 42;

void validate_metadata(char *parser_path, char *metadata_path)
{
	int ret = 0;
	char parser_output_path[] = "/tmp/parser_output_XXXXXX";
	int parser_output_fd = -1, metadata_fd = -1;

	if (!metadata_path) {
		ret = -1;
		goto result;
	}

	parser_output_fd = mkstemp(parser_output_path);
	metadata_fd = open(metadata_path, O_RDONLY);

	unlink(parser_output_path);

	if (parser_output_fd == -1 || metadata_fd == -1) {
		diag("Failed create temporary files for metadata parsing.");
		ret = -1;
		goto result;
	}

	pid_t pid = fork();
	if (pid) {
		int status = 0;
		waitpid(pid, &status, 0);
		ret = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	} else {
		/* ctf-parser-test expects a metadata string on stdin. */
		ret = dup2(metadata_fd, STDIN_FILENO);
		if (ret < 0) {
			perror("# dup2 metadata_fd to STDIN");
			goto result;
		}

		ret = dup2(parser_output_fd, STDOUT_FILENO);
		if (ret < 0) {
			perror("# dup2 parser_output_fd to STDOUT");
			goto result;
		}

		ret = dup2(parser_output_fd, STDERR_FILENO);
		if (ret < 0) {
			perror("# dup2 parser_output_fd to STDERR");
			goto result;
		}

		execl(parser_path, "ctf-parser-test", NULL);
		perror("# Could not launch the ctf metadata parser process");
		exit(-1);
	}
result:
	ok(ret == 0, "Metadata string is valid");

	if (ret && metadata_fd >= 0 && parser_output_fd >= 0) {
		char *line;
		size_t len = METADATA_LINE_SIZE;
		FILE *metadata_fp = NULL, *parser_output_fp = NULL;

		metadata_fp = fdopen(metadata_fd, "r");
		if (!metadata_fp) {
			perror("fdopen on metadata_fd");
			goto close_fp;
		}
		metadata_fd = -1;

		parser_output_fp = fdopen(parser_output_fd, "r");
		if (!parser_output_fp) {
			perror("fdopen on parser_output_fd");
			goto close_fp;
		}
		parser_output_fd = -1;

		line = malloc(len);
		if (!line) {
			diag("malloc failure");
		}

		rewind(metadata_fp);

		/* Output the metadata and parser output as diagnostic */
		while (getline(&line, &len, metadata_fp) > 0) {
			diag("%s", line);
		}

		rewind(parser_output_fp);
		while (getline(&line, &len, parser_output_fp) > 0) {
			diag("%s", line);
		}

		free(line);
close_fp:
		if (metadata_fp) {
			if (fclose(metadata_fp)) {
				diag("fclose failure");
			}
		}
		if (parser_output_fp) {
			if (fclose(parser_output_fp)) {
				diag("fclose failure");
			}
		}
	}

	if (parser_output_fd >= 0) {
		if (close(parser_output_fd)) {
			diag("close error");
		}
	}
	if (metadata_fd >= 0) {
		if (close(metadata_fd)) {
			diag("close error");
		}
	}
}

void validate_trace(char *parser_path, char *trace_path)
{
	int ret = 0;
	char babeltrace_output_path[] = "/tmp/babeltrace_output_XXXXXX";
	int babeltrace_output_fd = -1;

	if (!trace_path) {
		ret = -1;
		goto result;
	}

	babeltrace_output_fd = mkstemp(babeltrace_output_path);
	unlink(babeltrace_output_path);

	if (babeltrace_output_fd == -1) {
		diag("Failed to create a temporary file for trace parsing.");
		ret = -1;
		goto result;
	}

	pid_t pid = fork();
	if (pid) {
		int status = 0;
		waitpid(pid, &status, 0);
		ret = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	} else {
		ret = dup2(babeltrace_output_fd, STDOUT_FILENO);
		if (ret < 0) {
			perror("# dup2 babeltrace_output_fd to STDOUT");
			goto result;
		}

		ret = dup2(babeltrace_output_fd, STDERR_FILENO);
		if (ret < 0) {
			perror("# dup2 babeltrace_output_fd to STDERR");
			goto result;
		}

		execl(parser_path, "babeltrace", trace_path, NULL);
		perror("# Could not launch the babeltrace process");
		exit(-1);
	}
result:
	ok(ret == 0, "Babeltrace could read the resulting trace");

	if (ret && babeltrace_output_fd >= 0) {
		char *line;
		size_t len = METADATA_LINE_SIZE;
		FILE *babeltrace_output_fp = NULL;

		babeltrace_output_fp = fdopen(babeltrace_output_fd, "r");
		if (!babeltrace_output_fp) {
			perror("fdopen on babeltrace_output_fd");
			goto close_fp;
		}
		babeltrace_output_fd = -1;

		line = malloc(len);
		if (!line) {
			diag("malloc error");
		}
		rewind(babeltrace_output_fp);
		while (getline(&line, &len, babeltrace_output_fp) > 0) {
			diag("%s", line);
		}

		free(line);
close_fp:
		if (babeltrace_output_fp) {
			if (fclose(babeltrace_output_fp)) {
				diag("fclose error");
			}
		}
	}

	if (babeltrace_output_fd >= 0) {
		if (close(babeltrace_output_fd)) {
			diag("close error");
		}
	}
}

void append_simple_event(struct bt_ctf_stream_class *stream_class,
		struct bt_ctf_stream *stream, struct bt_ctf_clock *clock)
{
	/* Create and add a simple event class */
	struct bt_ctf_event_class *simple_event_class =
		bt_ctf_event_class_create("Simple Event");
	struct bt_ctf_field_type *uint_12_type =
		bt_ctf_field_type_integer_create(12);
	struct bt_ctf_field_type *float_type =
		bt_ctf_field_type_floating_point_create();
	struct bt_ctf_field_type *enum_type =
		bt_ctf_field_type_enumeration_create(uint_12_type);
	struct bt_ctf_event *simple_event;
	struct bt_ctf_field *integer_field;
	struct bt_ctf_field *float_field;
	struct bt_ctf_field *enum_field;
	struct bt_ctf_field *enum_container_field;
	const char *mapping_name_test = "truie";
	const char *mapping_name;
	const double double_test_value = 3.1415;
	double ret_double;

	bt_ctf_field_type_set_alignment(float_type, 32);
	bt_ctf_field_type_floating_point_set_exponent_digits(float_type, 11);
	bt_ctf_field_type_floating_point_set_mantissa_digits(float_type, 53);

	ok(bt_ctf_field_type_enumeration_add_mapping(enum_type,
		"escaping; \"test\"", 0, 0) == 0,
		"Accept enumeration mapping strings containing quotes");
	ok(bt_ctf_field_type_enumeration_add_mapping(enum_type,
		"\tanother \'escaping\'\n test\"", 1, 4) == 0,
		"Accept enumeration mapping strings containing special characters");
	ok(bt_ctf_field_type_enumeration_add_mapping(enum_type,
		"event clock int float", 5, 22) == 0,
		"Accept enumeration mapping strings containing reserved keywords");
	bt_ctf_field_type_enumeration_add_mapping(enum_type,
		mapping_name_test, 42, 42);
	ok(bt_ctf_event_class_add_field(simple_event_class, enum_type,
		"enum_field") == 0, "Add enumeration field to event");

	ok(uint_12_type, "Create an unsigned integer type");
	bt_ctf_event_class_add_field(simple_event_class, uint_12_type,
		"integer_field");
	bt_ctf_event_class_add_field(simple_event_class, float_type,
		"float_field");
	bt_ctf_stream_class_add_event_class(stream_class,
		simple_event_class);

	simple_event = bt_ctf_event_create(simple_event_class);

	ok(simple_event,
		"Instantiate an event containing a single integer field");

	integer_field = bt_ctf_field_create(uint_12_type);
	bt_ctf_field_unsigned_integer_set_value(integer_field, 42);
	ok(bt_ctf_event_set_payload(simple_event, "integer_field",
		integer_field) == 0, "Use bt_ctf_event_set_payload to set a manually allocated field");

	float_field = bt_ctf_event_get_payload(simple_event, "float_field");
	ok(bt_ctf_field_floating_point_get_value(float_field, &ret_double),
		"bt_ctf_field_floating_point_get_value fails on an unset float field");
	bt_ctf_field_floating_point_set_value(float_field, double_test_value);
	ok(bt_ctf_field_floating_point_get_value(NULL, &ret_double),
		"bt_ctf_field_floating_point_get_value properly handles a NULL field");
	ok(bt_ctf_field_floating_point_get_value(float_field, NULL),
		"bt_ctf_field_floating_point_get_value properly handles a NULL return value pointer");
	ok(!bt_ctf_field_floating_point_get_value(float_field, &ret_double),
		"bt_ctf_field_floating_point_get_value returns a double value");
	ok(fabs(ret_double - double_test_value) <= DBL_EPSILON,
		"bt_ctf_field_floating_point_get_value returns a correct value");

	enum_field = bt_ctf_field_create(enum_type);
	mapping_name = bt_ctf_field_enumeration_get_mapping_name(NULL);
	ok(!mapping_name, "bt_ctf_field_enumeration_get_mapping_name handles NULL correctly");
	mapping_name = bt_ctf_field_enumeration_get_mapping_name(enum_field);
	ok(!mapping_name, "bt_ctf_field_enumeration_get_mapping_name returns NULL if the enumeration's container field is unset");
	enum_container_field = bt_ctf_field_enumeration_get_container(
		enum_field);
	ok(bt_ctf_field_unsigned_integer_set_value(
		enum_container_field, 42) == 0,
		"Set enumeration container value");
	mapping_name = bt_ctf_field_enumeration_get_mapping_name(enum_field);
	ok(!strcmp(mapping_name, mapping_name_test), "bt_ctf_field_enumeration_get_mapping_name returns the correct mapping name");
	bt_ctf_event_set_payload(simple_event, "enum_field", enum_field);

	ok(bt_ctf_clock_set_time(clock, current_time) == 0, "Set clock time");

	ok(bt_ctf_stream_append_event(stream, simple_event) == 0,
		"Append simple event to trace stream");

	ok(bt_ctf_stream_flush(stream) == 0,
		"Flush trace stream with one event");

	bt_ctf_event_class_put(simple_event_class);
	bt_ctf_event_put(simple_event);
	bt_ctf_field_type_put(uint_12_type);
	bt_ctf_field_type_put(float_type);
	bt_ctf_field_type_put(enum_type);
	bt_ctf_field_put(integer_field);
	bt_ctf_field_put(float_field);
	bt_ctf_field_put(enum_field);
	bt_ctf_field_put(enum_container_field);
}

void append_complex_event(struct bt_ctf_stream_class *stream_class,
		struct bt_ctf_stream *stream, struct bt_ctf_clock *clock)
{
	int i;
	const char *test_string = "Test string";
	struct bt_ctf_field_type *uint_35_type =
		bt_ctf_field_type_integer_create(35);
	struct bt_ctf_field_type *int_16_type =
		bt_ctf_field_type_integer_create(16);
	struct bt_ctf_field_type *uint_3_type =
		bt_ctf_field_type_integer_create(3);
	struct bt_ctf_field_type *enum_variant_type =
		bt_ctf_field_type_enumeration_create(uint_3_type);
	struct bt_ctf_field_type *variant_type =
		bt_ctf_field_type_variant_create(enum_variant_type,
			"variant_selector");
	struct bt_ctf_field_type *string_type =
		bt_ctf_field_type_string_create();
	struct bt_ctf_field_type *sequence_type;
	struct bt_ctf_field_type *inner_structure_type =
		bt_ctf_field_type_structure_create();
	struct bt_ctf_field_type *complex_structure_type =
		bt_ctf_field_type_structure_create();
	struct bt_ctf_event_class *event_class;
	struct bt_ctf_event *event;
	struct bt_ctf_field *uint_35_field, *int_16_field, *a_string_field,
		*inner_structure_field, *complex_structure_field,
		*a_sequence_field, *enum_variant_field, *enum_container_field,
		*variant_field, *ret_field;
	int64_t ret_signed_int;
	uint64_t ret_unsigned_int;
	const char *ret_string;

	bt_ctf_field_type_set_alignment(int_16_type, 32);
	bt_ctf_field_type_integer_set_signed(int_16_type, 1);
	bt_ctf_field_type_integer_set_base(uint_35_type,
		BT_CTF_INTEGER_BASE_HEXADECIMAL);

	sequence_type = bt_ctf_field_type_sequence_create(int_16_type,
		"seq_len");
	bt_ctf_field_type_structure_add_field(inner_structure_type,
		uint_35_type, "seq_len");
	bt_ctf_field_type_structure_add_field(inner_structure_type,
		sequence_type, "a_sequence");

	bt_ctf_field_type_enumeration_add_mapping(enum_variant_type,
		"UINT3_TYPE", 0, 0);
	bt_ctf_field_type_enumeration_add_mapping(enum_variant_type,
		"INT16_TYPE", 1, 1);
	bt_ctf_field_type_enumeration_add_mapping(enum_variant_type,
		"UINT35_TYPE", 2, 7);
	ok(bt_ctf_field_type_variant_add_field(variant_type, uint_3_type,
		"An unknown entry"), "Reject a variant field based on an unknown tag value");
	ok(bt_ctf_field_type_variant_add_field(variant_type, uint_3_type,
		"UINT3_TYPE") == 0, "Add a field to a variant");
	bt_ctf_field_type_variant_add_field(variant_type, int_16_type,
		"INT16_TYPE");
	bt_ctf_field_type_variant_add_field(variant_type, uint_35_type,
		"UINT35_TYPE");

	bt_ctf_field_type_structure_add_field(complex_structure_type,
		enum_variant_type, "variant_selector");
	bt_ctf_field_type_structure_add_field(complex_structure_type,
		string_type, "a_string");
	bt_ctf_field_type_structure_add_field(complex_structure_type,
		variant_type, "variant_value");
	bt_ctf_field_type_structure_add_field(complex_structure_type,
		inner_structure_type, "inner_structure");

	ok(bt_ctf_event_class_create("clock") == NULL,
		"Reject creation of an event class with an illegal name");
	event_class = bt_ctf_event_class_create("Complex Test Event");
	ok(event_class, "Create an event class");
	ok(bt_ctf_event_class_add_field(event_class, uint_35_type, ""),
		"Reject addition of a field with an empty name to an event");
	ok(bt_ctf_event_class_add_field(event_class, NULL, "an_integer"),
		"Reject addition of a field with a NULL type to an event");
	ok(bt_ctf_event_class_add_field(event_class, uint_35_type,
		"int"),
		"Reject addition of a type with an illegal name to an event");
	ok(bt_ctf_event_class_add_field(event_class, uint_35_type,
		"uint_35") == 0,
		"Add field of type unsigned integer to an event");
	ok(bt_ctf_event_class_add_field(event_class, int_16_type,
		"int_16") == 0, "Add field of type signed integer to an event");
	ok(bt_ctf_event_class_add_field(event_class, complex_structure_type,
		"complex_structure") == 0,
		"Add composite structure to an event");

	/* Add event class to the stream class */
	ok(bt_ctf_stream_class_add_event_class(stream_class, NULL),
		"Reject addition of NULL event class to a stream class");
	ok(bt_ctf_stream_class_add_event_class(stream_class,
		event_class) == 0, "Add an event class to stream class");

	event = bt_ctf_event_create(event_class);
	ok(event, "Instanciate a complex event");

	uint_35_field = bt_ctf_event_get_payload(event, "uint_35");
	if (!uint_35_field)
		printf("uint_35_field is NULL\n");
	ok(uint_35_field, "Use bt_ctf_event_get_payload to get a field instance ");
	bt_ctf_field_unsigned_integer_set_value(uint_35_field, 0x0DDF00D);
	ok(bt_ctf_field_unsigned_integer_get_value(NULL, &ret_unsigned_int) == -1,
		"bt_ctf_field_unsigned_integer_get_value properly properly handles a NULL field.");
	ok(bt_ctf_field_unsigned_integer_get_value(uint_35_field, NULL) == -1,
		"bt_ctf_field_unsigned_integer_get_value properly handles a NULL return value");
	ok(bt_ctf_field_unsigned_integer_get_value(uint_35_field,
		&ret_unsigned_int) == 0,
		"bt_ctf_field_unsigned_integer_get_value succeeds after setting a value");
	ok(ret_unsigned_int == 0x0DDF00D,
		"bt_ctf_field_unsigned_integer_get_value returns the correct value");
	ok(bt_ctf_field_signed_integer_get_value(uint_35_field,
		&ret_signed_int) == -1,
		"bt_ctf_field_signed_integer_get_value fails on an unsigned field");
	bt_ctf_field_put(uint_35_field);

	int_16_field = bt_ctf_event_get_payload(event, "int_16");
	bt_ctf_field_signed_integer_set_value(int_16_field, -12345);
	ok(bt_ctf_field_signed_integer_get_value(NULL, &ret_signed_int) == -1,
		"bt_ctf_field_signed_integer_get_value properly handles a NULL field");
	ok(bt_ctf_field_signed_integer_get_value(int_16_field, NULL) == -1,
		"bt_ctf_field_signed_integer_get_value properly handles a NULL return value");
	ok(bt_ctf_field_signed_integer_get_value(int_16_field,
		&ret_signed_int) == 0,
		"bt_ctf_field_signed_integer_get_value succeeds after setting a value");
	ok(ret_signed_int == -12345,
		"bt_ctf_field_signed_integer_get_value returns the correct value");
	ok(bt_ctf_field_unsigned_integer_get_value(int_16_field,
		&ret_unsigned_int) == -1,
		"bt_ctf_field_unsigned_integer_get_value fails on a signed field");
	bt_ctf_field_put(int_16_field);

	complex_structure_field = bt_ctf_event_get_payload(event,
		"complex_structure");

	ok(bt_ctf_field_structure_get_field_by_index(NULL, 0) == NULL,
		"bt_ctf_field_structure_get_field_by_index handles NULL correctly");
	ok(bt_ctf_field_structure_get_field_by_index(NULL, 9) == NULL,
		"bt_ctf_field_structure_get_field_by_index handles an invalid index correctly");
	inner_structure_field = bt_ctf_field_structure_get_field_by_index(
		complex_structure_field, 3);
	ret_field_type = bt_ctf_field_get_type(inner_structure_field);
	bt_ctf_field_put(inner_structure_field);
	ok(ret_field_type == inner_structure_type,
		"bt_ctf_field_structure_get_field_by_index returns a correct field");
	bt_ctf_field_type_put(ret_field_type);

	inner_structure_field = bt_ctf_field_structure_get_field(
		complex_structure_field, "inner_structure");
	a_string_field = bt_ctf_field_structure_get_field(
		complex_structure_field, "a_string");
	enum_variant_field = bt_ctf_field_structure_get_field(
		complex_structure_field, "variant_selector");
	variant_field = bt_ctf_field_structure_get_field(
		complex_structure_field, "variant_value");
	uint_35_field = bt_ctf_field_structure_get_field(
		inner_structure_field, "seq_len");
	a_sequence_field = bt_ctf_field_structure_get_field(
		inner_structure_field, "a_sequence");

	enum_container_field = bt_ctf_field_enumeration_get_container(
		enum_variant_field);
	bt_ctf_field_unsigned_integer_set_value(enum_container_field, 1);
	int_16_field = bt_ctf_field_variant_get_field(variant_field,
		enum_variant_field);
	bt_ctf_field_signed_integer_set_value(int_16_field, -200);
	bt_ctf_field_put(int_16_field);
	ok(!bt_ctf_field_string_get_value(a_string_field),
		"bt_ctf_field_string_get_value returns NULL on an unset field");
	bt_ctf_field_string_set_value(a_string_field,
		test_string);
	ok(!bt_ctf_field_string_get_value(NULL),
		"bt_ctf_field_string_get_value correctly handles NULL");
	ret_string = bt_ctf_field_string_get_value(a_string_field);
	ok(ret_string, "bt_ctf_field_string_get_value returns a string");
	ok(!strcmp(ret_string, test_string),
		"bt_ctf_field_string_get_value returns a correct value");
	bt_ctf_field_unsigned_integer_set_value(uint_35_field,
		SEQUENCE_TEST_LENGTH);

	ok(bt_ctf_field_sequence_get_length(a_sequence_field) == NULL,
		"bt_ctf_field_sequence_get_length returns NULL when length is unset");
	ok(bt_ctf_field_sequence_set_length(a_sequence_field,
		uint_35_field) == 0, "Set a sequence field's length");
	ret_field = bt_ctf_field_sequence_get_length(a_sequence_field);
	ok(ret_field == uint_35_field,
		"bt_ctf_field_sequence_get_length returns the correct length field");
	ok(bt_ctf_field_sequence_get_length(NULL) == NULL,
		"bt_ctf_field_sequence_get_length properly handles NULL");

	for (i = 0; i < SEQUENCE_TEST_LENGTH; i++) {
		int_16_field = bt_ctf_field_sequence_get_field(
			a_sequence_field, i);
		bt_ctf_field_signed_integer_set_value(int_16_field, 4 - i);
		bt_ctf_field_put(int_16_field);
	}

	bt_ctf_clock_set_time(clock, ++current_time);
	ok(bt_ctf_stream_append_event(stream, event) == 0,
		"Append a complex event to a stream");
	ok(bt_ctf_stream_flush(stream) == 0,
		"Flush a stream containing a complex event");

	bt_ctf_field_put(uint_35_field);
	bt_ctf_field_put(a_string_field);
	bt_ctf_field_put(inner_structure_field);
	bt_ctf_field_put(complex_structure_field);
	bt_ctf_field_put(a_sequence_field);
	bt_ctf_field_put(enum_variant_field);
	bt_ctf_field_put(enum_container_field);
	bt_ctf_field_put(variant_field);
	bt_ctf_field_put(ret_field);
	bt_ctf_field_type_put(uint_35_type);
	bt_ctf_field_type_put(int_16_type);
	bt_ctf_field_type_put(string_type);
	bt_ctf_field_type_put(sequence_type);
	bt_ctf_field_type_put(inner_structure_type);
	bt_ctf_field_type_put(complex_structure_type);
	bt_ctf_field_type_put(uint_3_type);
	bt_ctf_field_type_put(enum_variant_type);
	bt_ctf_field_type_put(variant_type);
	bt_ctf_event_class_put(event_class);
	bt_ctf_event_put(event);
}

void type_field_tests()
{
	struct bt_ctf_field *uint_12;
	struct bt_ctf_field *int_16;
	struct bt_ctf_field *string;
	struct bt_ctf_field *enumeration;
	struct bt_ctf_field_type *composite_structure_type;
	struct bt_ctf_field_type *structure_seq_type;
	struct bt_ctf_field_type *string_type;
	struct bt_ctf_field_type *sequence_type;
	struct bt_ctf_field_type *uint_8_type;
	struct bt_ctf_field_type *int_16_type;
	struct bt_ctf_field_type *uint_12_type =
		bt_ctf_field_type_integer_create(12);
	struct bt_ctf_field_type *enumeration_type;
	struct bt_ctf_field_type *enumeration_sequence_type;
	struct bt_ctf_field_type *enumeration_array_type;
	struct bt_ctf_field_type *returned_type;

	returned_type = bt_ctf_field_get_type(NULL);
	ok(!returned_type, "bt_ctf_field_get_type handles NULL correctly");

	ok(uint_12_type, "Create an unsigned integer type");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_BINARY) == 0,
		"Set integer type's base as binary");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_DECIMAL) == 0,
		"Set integer type's base as decimal");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_UNKNOWN),
		"Reject integer type's base set as unknown");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_OCTAL) == 0,
		"Set integer type's base as octal");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_HEXADECIMAL) == 0,
		"Set integer type's base as hexadecimal");
	ok(bt_ctf_field_type_integer_set_base(uint_12_type, 457417),
		"Reject unknown integer base value");
	ok(bt_ctf_field_type_integer_set_signed(uint_12_type, 952835) == 0,
		"Set integer type signedness to signed");
	ok(bt_ctf_field_type_integer_set_signed(uint_12_type, 0) == 0,
		"Set integer type signedness to unsigned");

	int_16_type = bt_ctf_field_type_integer_create(16);
	bt_ctf_field_type_integer_set_signed(int_16_type, 1);
	uint_8_type = bt_ctf_field_type_integer_create(8);
	sequence_type =
		bt_ctf_field_type_sequence_create(int_16_type, "seq_len");
	ok(sequence_type, "Create a sequence of int16_t type");

	string_type = bt_ctf_field_type_string_create();
	ok(string_type, "Create a string type");
	ok(bt_ctf_field_type_string_set_encoding(string_type,
		CTF_STRING_NONE),
		"Reject invalid \"None\" string encoding");
	ok(bt_ctf_field_type_string_set_encoding(string_type,
		42),
		"Reject invalid string encoding");
	ok(bt_ctf_field_type_string_set_encoding(string_type,
		CTF_STRING_ASCII) == 0,
		"Set string encoding to ASCII");

	structure_seq_type = bt_ctf_field_type_structure_create();
	ok(structure_seq_type, "Create a structure type");
	ok(bt_ctf_field_type_structure_add_field(structure_seq_type,
		uint_8_type, "seq_len") == 0,
		"Add a uint8_t type to a structure");
	ok(bt_ctf_field_type_structure_add_field(structure_seq_type,
		sequence_type, "a_sequence") == 0,
		"Add a sequence type to a structure");
	composite_structure_type = bt_ctf_field_type_structure_create();
	ok(bt_ctf_field_type_structure_add_field(composite_structure_type,
		string_type, "a_string") == 0,
		"Add a string type to a structure");
	ok(bt_ctf_field_type_structure_add_field(composite_structure_type,
		structure_seq_type, "inner_structure") == 0,
		"Add a structure type to a structure");

	int_16 = bt_ctf_field_create(int_16_type);
	ok(int_16, "Instanciate a signed 16-bit integer");
	uint_12 = bt_ctf_field_create(uint_12_type);
	ok(uint_12, "Instanciate an unsigned 12-bit integer");
	returned_type = bt_ctf_field_get_type(int_16);
	ok(returned_type == int_16_type,
		"bt_ctf_field_get_type returns the correct type");
	bt_ctf_field_type_put(returned_type);

	/* Can't modify types after instanciating them */
	ok(bt_ctf_field_type_integer_set_base(uint_12_type,
		BT_CTF_INTEGER_BASE_DECIMAL),
		"Check an integer type' base can't be modified after instanciation");
	ok(bt_ctf_field_type_integer_set_signed(uint_12_type, 0),
		"Check an integer type's signedness can't be modified after instanciation");

	/* Check signed property is checked */
	ok(bt_ctf_field_signed_integer_set_value(uint_12, -52),
		"Check bt_ctf_field_signed_integer_set_value is not allowed on an unsigned integer");
	ok(bt_ctf_field_unsigned_integer_set_value(int_16, 42),
		"Check bt_ctf_field_unsigned_integer_set_value is not allowed on a signed integer");

	/* Check overflows are properly tested for */
	ok(bt_ctf_field_signed_integer_set_value(int_16, -32768) == 0,
		"Check -32768 is allowed for a signed 16-bit integer");
	ok(bt_ctf_field_signed_integer_set_value(int_16, 32767) == 0,
		"Check 32767 is allowed for a signed 16-bit integer");
	ok(bt_ctf_field_signed_integer_set_value(int_16, 32768),
		"Check 32768 is not allowed for a signed 16-bit integer");
	ok(bt_ctf_field_signed_integer_set_value(int_16, -32769),
		"Check -32769 is not allowed for a signed 16-bit integer");
	ok(bt_ctf_field_signed_integer_set_value(int_16, -42) == 0,
		"Check -42 is allowed for a signed 16-bit integer");

	ok(bt_ctf_field_unsigned_integer_set_value(uint_12, 4095) == 0,
		"Check 4095 is allowed for an unsigned 12-bit integer");
	ok(bt_ctf_field_unsigned_integer_set_value(uint_12, 4096),
		"Check 4096 is not allowed for a unsigned 12-bit integer");
	ok(bt_ctf_field_unsigned_integer_set_value(uint_12, 0) == 0,
		"Check 0 is allowed for an unsigned 12-bit integer");

	string = bt_ctf_field_create(string_type);
	ok(string, "Instanciate a string field");
	ok(bt_ctf_field_string_set_value(string, "A value") == 0,
		"Set a string's value");

	enumeration_type = bt_ctf_field_type_enumeration_create(uint_12_type);
	ok(enumeration_type,
		"Create an enumeration type with an unsigned 12-bit integer as container");
	enumeration_sequence_type = bt_ctf_field_type_sequence_create(
		enumeration_type, "count");
	ok(!enumeration_sequence_type,
		"Check enumeration types are validated when creating a sequence");
	enumeration_array_type = bt_ctf_field_type_array_create(
		enumeration_type, 10);
	ok(!enumeration_array_type,
		"Check enumeration types are validated when creating an array");
	ok(bt_ctf_field_type_structure_add_field(composite_structure_type,
		enumeration_type, "enumeration") == 0,
		"Check enumeration types are validated when adding them as structure members");
	enumeration = bt_ctf_field_create(enumeration_type);
	ok(!enumeration,
		"Check enumeration types are validated before instantiation");

	bt_ctf_field_put(string);
	bt_ctf_field_put(uint_12);
	bt_ctf_field_put(int_16);
	bt_ctf_field_put(enumeration);
	bt_ctf_field_type_put(composite_structure_type);
	bt_ctf_field_type_put(structure_seq_type);
	bt_ctf_field_type_put(string_type);
	bt_ctf_field_type_put(sequence_type);
	bt_ctf_field_type_put(uint_8_type);
	bt_ctf_field_type_put(int_16_type);
	bt_ctf_field_type_put(uint_12_type);
	bt_ctf_field_type_put(enumeration_type);
	bt_ctf_field_type_put(enumeration_sequence_type);
	bt_ctf_field_type_put(enumeration_array_type);
}

void packet_resize_test(struct bt_ctf_stream_class *stream_class,
		struct bt_ctf_stream *stream, struct bt_ctf_clock *clock)
{
	/*
	 * Append enough events to force the underlying packet to be resized.
	 * Also tests that a new event can be declared after a stream has been
	 * instantiated and used/flushed.
	 */
	int ret = 0;
	int i;
	struct bt_ctf_event_class *event_class = bt_ctf_event_class_create(
		"Spammy_Event");
	struct bt_ctf_field_type *integer_type =
		bt_ctf_field_type_integer_create(17);
	struct bt_ctf_field_type *string_type =
		bt_ctf_field_type_string_create();

	ret |= bt_ctf_event_class_add_field(event_class, integer_type,
		"field_1");
	ret |= bt_ctf_event_class_add_field(event_class, string_type,
		"a_string");
	ret |= bt_ctf_stream_class_add_event_class(stream_class, event_class);
	ok(ret == 0, "Add a new event class to a stream class after writing an event");
	if (ret) {
		goto end;
	}

	for (i = 0; i < PACKET_RESIZE_TEST_LENGTH; i++) {
		struct bt_ctf_event *event = bt_ctf_event_create(event_class);
		struct bt_ctf_field *integer =
			bt_ctf_field_create(integer_type);
		struct bt_ctf_field *string =
			bt_ctf_field_create(string_type);

		ret |= bt_ctf_clock_set_time(clock, ++current_time);
		ret |= bt_ctf_field_unsigned_integer_set_value(integer, i);
		ret |= bt_ctf_event_set_payload(event, "field_1",
			integer);
		bt_ctf_field_put(integer);
		ret |= bt_ctf_field_string_set_value(string, "This is a test");
		ret |= bt_ctf_event_set_payload(event, "a_string",
			string);
		bt_ctf_field_put(string);
		ret |= bt_ctf_stream_append_event(stream, event);
		bt_ctf_event_put(event);

		if (ret) {
			break;
		}
	}
end:
	ok(ret == 0, "Append 100 000 events to a stream");
	ok(bt_ctf_stream_flush(stream) == 0,
		"Flush a stream that forces a packet resize");
	bt_ctf_field_type_put(integer_type);
	bt_ctf_field_type_put(string_type);
	bt_ctf_event_class_put(event_class);
}

int main(int argc, char **argv)
{
	char trace_path[] = "/tmp/ctfwriter_XXXXXX";
	char metadata_path[sizeof(trace_path) + 9];
	const char *clock_name = "test_clock";
	const char *clock_description = "This is a test clock";
	const char *returned_clock_name;
	const char *returned_clock_description;
	const uint64_t frequency = 1123456789;
	const uint64_t offset_s = 1351530929945824323;
	const uint64_t offset = 1234567;
	const uint64_t precision = 10;
	const int is_absolute = 0xFF;
	char *metadata_string;
	struct bt_ctf_writer *writer;
	struct utsname name;
	char hostname[HOST_NAME_MAX];
	struct bt_ctf_clock *clock;
	struct bt_ctf_stream_class *stream_class;
	struct bt_ctf_stream *stream1;

	if (argc < 3) {
		printf("Usage: tests-ctf-writer path_to_ctf_parser_test path_to_babeltrace\n");
		exit(-1);
	}

	plan_no_plan();

	if (!mkdtemp(trace_path)) {
		perror("# perror");
	}

	strcpy(metadata_path, trace_path);
	strcat(metadata_path + sizeof(trace_path) - 1, "/metadata");

	writer = bt_ctf_writer_create(trace_path);
	ok(writer, "bt_ctf_create succeeds in creating trace with path");

	/* Add environment context to the trace */
	gethostname(hostname, HOST_NAME_MAX);
	ok(bt_ctf_writer_add_environment_field(writer, "host", hostname) == 0,
		"Add host (%s) environment field to writer instance",
		hostname);
	ok(bt_ctf_writer_add_environment_field(NULL, "test_field",
		"test_value"),
		"bt_ctf_writer_add_environment_field error with NULL writer");
	ok(bt_ctf_writer_add_environment_field(writer, NULL,
		"test_value"),
		"bt_ctf_writer_add_environment_field error with NULL field name");
	ok(bt_ctf_writer_add_environment_field(writer, "test_field",
		NULL),
		"bt_ctf_writer_add_environment_field error with NULL field value");

	if (uname(&name)) {
		perror("uname");
		return -1;
	}

	ok(bt_ctf_writer_add_environment_field(writer, "sysname", name.sysname)
		== 0, "Add sysname (%s) environment field to writer instance",
		name.sysname);
	ok(bt_ctf_writer_add_environment_field(writer, "nodename",
		name.nodename) == 0,
		"Add nodename (%s) environment field to writer instance",
		name.nodename);
	ok(bt_ctf_writer_add_environment_field(writer, "release", name.release)
		== 0, "Add release (%s) environment field to writer instance",
		name.release);
	ok(bt_ctf_writer_add_environment_field(writer, "version", name.version)
		== 0, "Add version (%s) environment field to writer instance",
		name.version);
	ok(bt_ctf_writer_add_environment_field(writer, "machine", name.machine)
		== 0, "Add machine (%s) environment field to writer istance",
		name.machine);

	/* Define a clock and add it to the trace */
	ok(bt_ctf_clock_create("signed") == NULL,
		"Illegal clock name rejected");
	ok(bt_ctf_clock_create(NULL) == NULL, "NULL clock name rejected");
	clock = bt_ctf_clock_create(clock_name);
	ok(clock, "Clock created sucessfully");
	returned_clock_name = bt_ctf_clock_get_name(clock);
	ok(returned_clock_name, "bt_ctf_clock_get_name returns a clock name");
	ok(strcmp(returned_clock_name, clock_name) == 0,
		"Returned clock name is valid");

	returned_clock_description = bt_ctf_clock_get_description(clock);
	ok(!returned_clock_description, "bt_ctf_clock_get_description returns NULL on an unset description");
	ok(bt_ctf_clock_set_description(clock, clock_description) == 0,
		"Clock description set successfully");

	returned_clock_description = bt_ctf_clock_get_description(clock);
	ok(returned_clock_description,
		"bt_ctf_clock_get_description returns a description.");
	ok(strcmp(returned_clock_description, clock_description) == 0,
		"Returned clock description is valid");

	ok(bt_ctf_clock_get_frequency(clock) == DEFAULT_CLOCK_FREQ,
		"bt_ctf_clock_get_frequency returns the correct default frequency");
	ok(bt_ctf_clock_set_frequency(clock, frequency) == 0,
		"Set clock frequency");
	ok(bt_ctf_clock_get_frequency(clock) == frequency,
		"bt_ctf_clock_get_frequency returns the correct frequency once it is set");

	ok(bt_ctf_clock_get_offset_s(clock) == DEFAULT_CLOCK_OFFSET_S,
		"bt_ctf_clock_get_offset_s returns the correct default offset (in seconds)");
	ok(bt_ctf_clock_set_offset_s(clock, offset_s) == 0,
		"Set clock offset (seconds)");
	ok(bt_ctf_clock_get_offset_s(clock) == offset_s,
		"bt_ctf_clock_get_offset_s returns the correct default offset (in seconds) once it is set");

	ok(bt_ctf_clock_get_offset(clock) == DEFAULT_CLOCK_OFFSET,
		"bt_ctf_clock_get_frequency returns the correct default offset (in ticks)");
	ok(bt_ctf_clock_set_offset(clock, offset) == 0, "Set clock offset");
	ok(bt_ctf_clock_get_offset(clock) == offset,
		"bt_ctf_clock_get_frequency returns the correct default offset (in ticks) once it is set");

	ok(bt_ctf_clock_get_precision(clock) == DEFAULT_CLOCK_PRECISION,
		"bt_ctf_clock_get_precision returns the correct default precision");
	ok(bt_ctf_clock_set_precision(clock, precision) == 0,
		"Set clock precision");
	ok(bt_ctf_clock_get_precision(clock) == precision,
		"bt_ctf_clock_get_precision returns the correct precision once it is set");

	ok(bt_ctf_clock_get_is_absolute(clock) == DEFAULT_CLOCK_IS_ABSOLUTE,
		"bt_ctf_clock_get_precision returns the correct default is_absolute attribute");
	ok(bt_ctf_clock_set_is_absolute(clock, is_absolute) == 0,
		"Set clock absolute property");
	ok(bt_ctf_clock_get_is_absolute(clock) == !!is_absolute,
		"bt_ctf_clock_get_precision returns the correct is_absolute attribute once it is set");

	ok(bt_ctf_clock_get_time(clock) == DEFAULT_CLOCK_TIME,
		"bt_ctf_clock_get_time returns the correct default time");
	ok(bt_ctf_clock_set_time(clock, current_time) == 0,
		"Set clock time");
	ok(bt_ctf_clock_get_time(clock) == current_time,
		"bt_ctf_clock_get_time returns the correct time once it is set");

	ok(bt_ctf_writer_add_clock(writer, clock) == 0,
		"Add clock to writer instance");
	ok(bt_ctf_writer_add_clock(writer, clock),
		"Verify a clock can't be added twice to a writer instance");

	ok(!bt_ctf_clock_get_name(NULL),
		"bt_ctf_clock_get_name correctly handles NULL");
	ok(!bt_ctf_clock_get_description(NULL),
		"bt_ctf_clock_get_description correctly handles NULL");
	ok(bt_ctf_clock_get_frequency(NULL) == -1ULL,
		"bt_ctf_clock_get_frequency correctly handles NULL");
	ok(bt_ctf_clock_get_precision(NULL) == -1ULL,
		"bt_ctf_clock_get_precision correctly handles NULL");
	ok(bt_ctf_clock_get_offset_s(NULL) == -1ULL,
		"bt_ctf_clock_get_offset_s correctly handles NULL");
	ok(bt_ctf_clock_get_offset(NULL) == -1ULL,
		"bt_ctf_clock_get_offset correctly handles NULL");
	ok(bt_ctf_clock_get_is_absolute(NULL) == -1,
		"bt_ctf_clock_get_is_absolute correctly handles NULL");
	ok(bt_ctf_clock_get_time(NULL) == -1ULL,
		"bt_ctf_clock_get_time correctly handles NULL");

	ok(bt_ctf_clock_set_description(NULL, NULL) == -1,
		"bt_ctf_clock_set_description correctly handles NULL clock");
	ok(bt_ctf_clock_set_frequency(NULL, frequency) == -1,
		"bt_ctf_clock_set_frequency correctly handles NULL clock");
	ok(bt_ctf_clock_set_precision(NULL, precision) == -1,
		"bt_ctf_clock_get_precision correctly handles NULL clock");
	ok(bt_ctf_clock_set_offset_s(NULL, offset_s) == -1,
		"bt_ctf_clock_set_offset_s correctly handles NULL clock");
	ok(bt_ctf_clock_set_offset(NULL, offset) == -1,
		"bt_ctf_clock_set_offset correctly handles NULL clock");
	ok(bt_ctf_clock_set_is_absolute(NULL, is_absolute) == -1,
		"bt_ctf_clock_set_is_absolute correctly handles NULL clock");
	ok(bt_ctf_clock_set_time(NULL, current_time) == -1,
		"bt_ctf_clock_set_time correctly handles NULL clock");

	/* Define a stream class */
	stream_class = bt_ctf_stream_class_create("test_stream");
	ok(stream_class, "Create stream class");
	ok(bt_ctf_stream_class_set_clock(stream_class, clock) == 0,
		"Set a stream class' clock");

	/* Test the event fields and event types APIs */
	type_field_tests();

	/* Instantiate a stream and append events */
	stream1 = bt_ctf_writer_create_stream(writer, stream_class);
	ok(stream1, "Instanciate a stream class from writer");

	/* Should fail after instanciating a stream (locked)*/
	ok(bt_ctf_stream_class_set_clock(stream_class, clock),
		"Changes to a stream class that was already instantiated fail");

	append_simple_event(stream_class, stream1, clock);

	packet_resize_test(stream_class, stream1, clock);

	append_complex_event(stream_class, stream1, clock);

	metadata_string = bt_ctf_writer_get_metadata_string(writer);
	ok(metadata_string, "Get metadata string");

	bt_ctf_writer_flush_metadata(writer);
	validate_metadata(argv[1], metadata_path);
	validate_trace(argv[2], trace_path);

	bt_ctf_clock_put(clock);
	bt_ctf_stream_class_put(stream_class);
	bt_ctf_writer_put(writer);
	bt_ctf_stream_put(stream1);
	free(metadata_string);

	/* Remove all trace files and delete temporary trace directory */
	DIR *trace_dir = opendir(trace_path);
	if (!trace_dir) {
		perror("# opendir");
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(trace_dir))) {
		if (entry->d_type == DT_REG) {
			unlinkat(dirfd(trace_dir), entry->d_name, 0);
		}
	}

	rmdir(trace_path);
	closedir(trace_dir);

	return 0;
}
