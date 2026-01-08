/*
  +----------------------------------------------------------------------+
  | See LICENSE file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Authors: Ioseb Dzmanashvili <ioseb.dzmanashvili@gmail.com>           |
  +----------------------------------------------------------------------+
*/

#include "php_uri_template.h"

#define SHIFT_BACK(var) (!var->explode ? var->length == 0 \
	? 0 : ((int) log10(var->length) + 1) + 1 : 1)

#define IS_OPERATOR(c) c == '+' || c == '#' || c == '.' || \
	c == '/' || c == ';' || c == '?' || c == '&'

inline static int extract_num(char *str, int len);
inline static void append_malformed_expr(smart_str *dest, char *tpl, int len);
static void add_expr_to_array(zval *expressions, uri_template_expr *expr);
static uri_template_expr *build_expr(char *tpl, int len);

inline static int extract_num(char *str, int len)
{
	char *buff;
	int ret;

	buff = emalloc(sizeof(char) * (len+1));

	strncpy(buff, str, len);
	buff[len] = 0;

	ret = atoi(buff);

	efree(buff);

	return ret;
}

static uri_template_expr *build_expr(char *tpl, int len)
{
	uri_template_expr *expr;
	uri_template_var  *var = uri_template_var_create();
	char *name = tpl, *start = tpl, *prefix;
	
	if (IS_OPERATOR(*tpl)) {
		expr = uri_template_expr_create(*tpl++);
		name = tpl;
	} else {
		expr = uri_template_expr_create(0);
	}
	
	while (tpl - start <= len) {
		switch(*tpl) {
			case '%':
				if (name + len - tpl > 2) {
					if (isxdigit(*(tpl + 1))) {
						if (isxdigit(*(++tpl + 1))) {
							tpl++;
						} else {
							expr->error = URI_TEMPLATE_ERROR;
						}
					} else {
						expr->error = URI_TEMPLATE_ERROR;
					}
				} else {
					expr->error = URI_TEMPLATE_ERROR;
				}
				
				break;
			case ':':
				prefix = ++tpl;

				if (*tpl >= '1' && *tpl++ <= '9') {
					while (isdigit(*tpl)) {
						tpl++;
					}
					
					if (*tpl == ',' || *tpl == '}') {
						if (tpl - prefix < 5) {
							var->length = extract_num(tpl - (tpl - prefix), tpl - prefix);
						} else {
							expr->error = URI_TEMPLATE_ERROR;
						}
					  
						tpl--;
					} else {
						expr->error = URI_TEMPLATE_ERROR;
					}
				} else {
					expr->error = URI_TEMPLATE_ERROR;
				}
				
				break;
			case '*':
				if (*(tpl + 1) == '}' || *(tpl + 1) == ',') {
					var->explode = 1;
				} else {
					expr->error = URI_TEMPLATE_ERROR;
				}
				
				break;
			case ',':
			case '}':
				var->name = estrndup(name, tpl - name - SHIFT_BACK(var));
				uri_template_expr_add_var(expr, var);
				
				if (*tpl == ',') {
					var = uri_template_var_create();
					name = tpl + 1;
				}
				
				break;
			default:
				expr->error |= !(isalnum(*tpl) || *tpl == '_' || *tpl == '.');
				
				break;
		}
		
		tpl++;
	}

	return expr;
}

inline static void append_malformed_expr(smart_str *dest, char *tpl, int len)
{
	smart_str_appendc(dest, '{');
	smart_str_appendl(dest, tpl, len);
	smart_str_appendc(dest, '}');
}

static void add_expr_to_array(zval *expressions, uri_template_expr *expr)
{
	uri_template_var *next;
	zval result;
	zval vars_arr;
	char op[2] = {expr->op, 0};
	char sep[2] = {expr->sep, 0};
	char ifemp[2] = {expr->ifemp, 0};

	array_init(&result);

	add_assoc_string(&result, "op", op);
	add_assoc_string(&result, "sep", sep);
	add_assoc_string(&result, "ifemp", ifemp);
	add_assoc_bool(&result, "allow", expr->allow);
	add_assoc_bool(&result, "named", expr->named);
	add_assoc_bool(&result, "error", expr->error);

	array_init_size(&vars_arr, expr->vars->count);

	next = expr->vars->first;

	while (next != NULL) {
		zval var;

		array_init(&var);

		add_assoc_string(&var, "name", next->name);
		add_assoc_long(&var, "length", next->length);
		add_assoc_bool(&var, "explode", next->explode);
		add_next_index_zval(&vars_arr, &var);

		next = next->next;
	}

	add_assoc_zval(&result, "vars", &vars_arr);
	add_next_index_zval(expressions, &result);
}

void uri_template_parse(char *tpl, zval *return_value, zval *vars, zval *capture)
{
	smart_str result = {0};
	zval expressions;
	zval vars_ptr;
	unsigned char c;
	char *start;
	int state = URI_TEMPLATE_ERROR_NONE;
	int has_expressions = 0;

	if (capture != NULL) {
		array_init(&expressions);
		has_expressions = 1;
	}

	ZVAL_DUP(&vars_ptr, vars);

	while (*tpl) {
		if (*tpl == '{') {
			start = tpl + 1;
			tpl++;

			while (*tpl && *tpl != '}' && *tpl != '{') {
				tpl++;
			}

			if (*tpl == '}') {
				if (tpl - start > 0) {
					uri_template_expr *expr = build_expr(start, tpl - start);

					if (expr->error) {
						append_malformed_expr(&result, start, tpl - start);

						if (state == URI_TEMPLATE_ERROR_NONE) {
							state = URI_TEMPLATE_ERROR_EXPRESSION;
						}
					} else {
						uri_template_process(expr, &vars_ptr, &result);
					}

					if (has_expressions) {
						add_expr_to_array(&expressions, expr);
					}

					uri_template_expr_free(expr);
				} else {
					smart_str_appends(&result, "{}");
				}
			} else if (*tpl == '{') {
				smart_str_appendl(&result, start - 1, tpl - start + 1);
				state = URI_TEMPLATE_ERROR_SYNTAX;
				tpl--;
			} else {
				/* Incomplete expression - hit end of string */
				smart_str_appendc(&result, '{');
				smart_str_appendl(&result, start, tpl - start);
				state = URI_TEMPLATE_ERROR_SYNTAX;
				break;
			}
		} else {
			c = *tpl;

			if (c == '}') {
				smart_str_appendc(&result, '}');
				state = URI_TEMPLATE_ERROR_SYNTAX;
			} else if (c == '%' && isxdigit(*(tpl + 1)) && isxdigit(*(tpl + 2))) {
				smart_str_appendc(&result, '%');
				smart_str_appendc(&result, *(++tpl));
				smart_str_appendc(&result, *(++tpl));
			} else {
				size_t result_len = result.s ? ZSTR_LEN(result.s) : 0;
				size_t distance = 0;

				uri_template_substr_copy(&result, tpl, 1, URI_TEMPLATE_ALLOW_RESERVED);

				distance = (result.s ? ZSTR_LEN(result.s) : 0) - result_len;
				tpl += (distance % 3 ? 1 : distance / 3) - 1;
			}
		}

		tpl++;
	}

	smart_str_0(&result);
	ZVAL_STRING(return_value, result.s ? ZSTR_VAL(result.s) : "");

	if (capture != NULL) {
		add_assoc_string(capture, "result", result.s ? ZSTR_VAL(result.s) : "");
		add_assoc_long(capture, "state", state);
		add_assoc_zval(capture, "expressions", &expressions);
	}

	zval_ptr_dtor(&vars_ptr);
	smart_str_free(&result);
}
