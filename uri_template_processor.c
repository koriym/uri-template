/*
  +----------------------------------------------------------------------+
  | See LICENSE file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Authors: Ioseb Dzmanashvili <ioseb.dzmanashvili@gmail.com>           |
  +----------------------------------------------------------------------+
*/

#include "php_uri_template.h"

#define URI_TEMPLATE_PROCESSING_ARGS \
	uri_template_expr *expr, uri_template_var *var, zval *vars, smart_str *result

#define ALLOWED_CHARS(expr) (expr->op == '+' || expr->op == '#' \
	? URI_TEMPLATE_ALLOW_RESERVED : URI_TEMPLATE_ALLOW_UNRESERVED)

inline static void copy_var_valuel(smart_str *dest, zend_string *val, uri_template_expr *expr, uri_template_var *var)
{
	size_t len = var->length && ((size_t)var->length < ZSTR_LEN(val))
		? (size_t)var->length : ZSTR_LEN(val);

	uri_template_substr_copy(dest, ZSTR_VAL(val), len, ALLOWED_CHARS(expr));
}

inline static void copy_var_value(smart_str *dest, zend_string *val, uri_template_expr *expr, uri_template_var *var)
{
	uri_template_substr_copy(dest, ZSTR_VAL(val), ZSTR_LEN(val), ALLOWED_CHARS(expr));
}

inline static void copy_var_name(smart_str *dest, uri_template_var *var)
{
	uri_template_substr_copy(dest, var->name, strlen(var->name), URI_TEMPLATE_ALLOW_UNRESERVED);
}

inline static zend_bool array_is_assoc(zval *array)
{
	zend_ulong num_key;
	zend_string *str_key;
	zval *entry;

	ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(array), num_key, str_key, entry) {
		(void)num_key;
		(void)entry;
		if (str_key) {
			return 1;
		}
	} ZEND_HASH_FOREACH_END();

	return 0;
}

static void process_associative_array(URI_TEMPLATE_PROCESSING_ARGS)
{
	zend_string *str_key;
	char separator = var->explode ? expr->sep : ',';
	int i = 0;
	zend_ulong num_key;
	zval *entry;

	ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(vars), num_key, str_key, entry) {
		zend_string *entry_str;
		(void)num_key;
		if (!str_key) {
			continue;
		}
		if (i > 0) {
			smart_str_appendc(result, separator);
		}

		entry_str = zval_get_string(entry);
		uri_template_substr_copy(result, ZSTR_VAL(str_key), ZSTR_LEN(str_key), URI_TEMPLATE_ALLOW_UNRESERVED);

		if (var->explode) {
			if (!ZSTR_LEN(entry_str)) {
				if (expr->ifemp) {
					smart_str_appendc(result, expr->ifemp);
				}
			} else {
				smart_str_appendc(result, '=');
			}

			copy_var_value(result, entry_str, expr, var);
		} else {
			if (ZSTR_LEN(entry_str)) {
				smart_str_appendc(result, ',');
				copy_var_value(result, entry_str, expr, var);
			}
		}

		zend_string_release(entry_str);
		i++;
	} ZEND_HASH_FOREACH_END();
}

static void process_indexed_array(URI_TEMPLATE_PROCESSING_ARGS)
{
	zval *entry;
	char separator = var->explode ? expr->sep : ',';
	int i = 0;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(vars), entry) {
		zend_string *entry_str;
		if (i > 0) {
			smart_str_appendc(result, separator);
		}

		entry_str = zval_get_string(entry);

		if (var->explode && expr->named) {
			copy_var_name(result, var);

			if (!ZSTR_LEN(entry_str)) {
				if (expr->ifemp) {
					smart_str_appendc(result, expr->ifemp);
				}
			} else {
				smart_str_appendc(result, '=');
			}
		}

		copy_var_value(result, entry_str, expr, var);
		zend_string_release(entry_str);

		i++;
	} ZEND_HASH_FOREACH_END();
}

static void process_var_array(URI_TEMPLATE_PROCESSING_ARGS)
{
	smart_str eval = {0};

	if (array_is_assoc(vars)) {
		process_associative_array(expr, var, vars, &eval);
	} else {
		process_indexed_array(expr, var, vars, &eval);
	}

	smart_str_0(&eval);

	if (!var->explode) {
		if (eval.s && ZSTR_LEN(eval.s)) {
			if (expr->named) {
				copy_var_name(result, var);
				smart_str_appendc(result, '=');
			}

			smart_str_appendl(result, ZSTR_VAL(eval.s), ZSTR_LEN(eval.s));
		} else {
			if (expr->named) {
				copy_var_name(result, var);

				if (expr->ifemp) {
					smart_str_appendc(result, expr->ifemp);
				}
			}
		}
	} else {
		if (eval.s) {
			smart_str_appendl(result, ZSTR_VAL(eval.s), ZSTR_LEN(eval.s));
		}
	}

	smart_str_free(&eval);
}

static zend_bool process_var(URI_TEMPLATE_PROCESSING_ARGS)
{
	zval *entry;
	zend_bool found;

	entry = zend_hash_str_find(Z_ARRVAL_P(vars), var->name, strlen(var->name));
	found = (entry != NULL && Z_TYPE_P(entry) != IS_NULL);

	if (found) {
		if (Z_TYPE_P(entry) == IS_ARRAY) {
			process_var_array(expr, var, entry, result);
			found = zend_hash_num_elements(Z_ARRVAL_P(entry)) > 0;
		} else {
			zend_string *entry_str = zval_get_string(entry);

			if (!expr->named) {
				copy_var_valuel(result, entry_str, expr, var);
			} else {
				copy_var_name(result, var);

				if (!ZSTR_LEN(entry_str)) {
					if (expr->ifemp) {
						smart_str_appendc(result, expr->ifemp);
					}
				} else {
					smart_str_appendc(result, '=');
					copy_var_valuel(result, entry_str, expr, var);
				}
			}

			zend_string_release(entry_str);
		}
	}

	return found;
}

void uri_template_process(uri_template_expr *expr, zval *vars, smart_str *result)
{
	uri_template_var *var = expr->vars->first;
	zend_bool status = 0;
	zend_bool processed = 0;
	int i = 0;

	while (var != NULL) {
		smart_str eval = {0};
		status = process_var(expr, var, vars, &eval);

		if (status) {
			smart_str_0(&eval);

			if (i == 0 && expr->first) {
				smart_str_appendc(result, expr->first);
				i++;
			} else if (processed) {
				smart_str_appendc(result, expr->sep);
			}

			if (eval.s) {
				smart_str_appendl(result, ZSTR_VAL(eval.s), ZSTR_LEN(eval.s));
			}
		}

		smart_str_free(&eval);
		processed |= status;
		var = var->next;
	}
}