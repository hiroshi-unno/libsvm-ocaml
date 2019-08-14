/*
   LIBSVM-OCaml - OCaml-bindings to the LIBSVM library

   Copyright (C) 2013-  Oliver Gu
   email: gu.oliver@yahoo.com

   Copyright (C) 2005  Dominik Brugger
   email: dominikbrugger@fastmail.fm
   WWW:   http://ocaml-libsvm.berlios.de

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
extern "C" {
#include <stdio.h>
#include <string.h>
#include <climits>
#include <cfloat>

#include <caml/mlvalues.h>
#include <caml/custom.h>
#include <caml/alloc.h>
#include <caml/memory.h>
#include <caml/fail.h>
#include <caml/signals.h>
#include <caml/bigarray.h>

    #include <cassert>
#include "svm.h"

#define max(x,y) (((x)>(y))?(x):(y))
#define min(x,y) (((x)<(y))?(x):(y))

/* Type definitions */

typedef struct svm_problem svm_prob;
typedef struct svm_parameter svm_param;
typedef struct svm_model svm_model;

#define Caml_stat_alloc(type,n) (type *)caml_stat_alloc((n)*sizeof(type))

/* Macros to access the wrapper structures stored in the custom blocks */

#define Svm_problem_val(x) ((*(svm_prob **) (Data_custom_val(x))))
#define Svm_param_val(x) ((*(svm_param **) (Data_custom_val(x))))
#define Svm_model_val(x) ((*(svm_model **) (Data_custom_val(x))))

/* Set quiet mode */

void print_null(const char *s) {}

CAMLprim value svm_set_quiet_mode_stub(value unit)
{
  CAMLparam0();
  svm_set_print_string_function(&print_null);
  CAMLreturn(Val_unit);
}

/* SVM Problem */

void finalize_problem_gc(value v_prob)
{
  svm_prob *prob = Svm_problem_val(v_prob);
  free(prob->x[0]);
  free(prob->x);
  free(prob->y);
}

CAMLprim value svm_problem_create_stub(value unit)
{
  CAMLparam1(unit);
  value v_prob = caml_alloc_final(1, finalize_problem_gc, 1, 100);
  Svm_problem_val(v_prob) = Caml_stat_alloc(svm_prob,1);
  CAMLreturn(v_prob);
}

CAMLprim value svm_problem_l_set_stub(value v_prob, value v_size)
{
  CAMLparam2(v_prob, v_size);
  Svm_problem_val(v_prob)->l = Int_val(v_size);
  CAMLreturn(Val_unit);
}

CAMLprim value svm_problem_l_get_stub(value v_prob)
{
  CAMLparam1(v_prob);
  CAMLreturn(Val_int(Svm_problem_val(v_prob)->l));
}

CAMLprim value svm_problem_y_set_stub(value v_prob, value v_y)
{
  CAMLparam2(v_prob, v_y);
  svm_problem* prob = Svm_problem_val(v_prob);
  assert(Caml_ba_array_val(v_y)->num_dims == 1);
  assert(Caml_ba_array_val(v_y)->flags & CAML_BA_FLOAT64);
  int m = Caml_ba_array_val(v_y)->dim[0];
  double* data = (double*) Caml_ba_data_val(v_y);

  prob->y = (double*) malloc(sizeof(double) * m);
  for (int i = 0; i < m; i++) prob->y[i] = data[i];
  CAMLreturn(Val_unit);
}

CAMLprim value svm_problem_y_get_stub(value v_prob, value v_idx)
{
  CAMLparam1(v_prob);
  svm_prob *prob = Svm_problem_val(v_prob);
  int i = Int_val(v_idx);
  CAMLreturn(caml_copy_double(prob->y[i]));
}

CAMLprim value svm_problem_x_dense_set_stub(value v_prob, value v_x)
{
  CAMLparam2(v_prob, v_x);
  svm_problem* prob = Svm_problem_val(v_prob);
  assert(Caml_ba_array_val(v_x)->num_dims == 2);
  assert(Caml_ba_array_val(v_x)->flags & CAML_BA_FLOAT64);

  int m = Caml_ba_array_val(v_x)->dim[0];
  int n = Caml_ba_array_val(v_x)->dim[1];
  double* data = (double*) Caml_ba_data_val(v_x);

  prob->x = (svm_node **) malloc(sizeof(svm_node*) * m);
  svm_node* buf = (svm_node*) malloc(sizeof(svm_node) * m * (n + 1));
  prob->x[0] = buf;
  for(int i = 1; i < m; i++)
    prob->x[i] = prob->x[i - 1] + n + 1;

  int k = 0;
  for(int j = 0; j < n; j++)
    for(int i = 0; i < m; i++, k++) {
      svm_node* n = prob->x[i] + j;
      n->index = j;
      n->value = data[k];
    }
  for(int i = 0; i < m; i++)
    prob->x[i][n].index = -1;

  CAMLreturn(Val_unit);
}

/* not meant to be called from ocaml */
void copy_sparse_feature_vector(value src, svm_node* dst) {
  int j = 0;
  for(value v = src;
      v != Val_emptylist;
      j++, v = Field(v, 1)) {
    svm_node* n = dst + j;
    value pair = Field(v, 0);
    n->index = Int_val(Field(pair, 0));
    n->value = Double_val(Field(pair, 1));
  }
  dst[j].index = -1;
}

int svm_list_length(value l) {
  int c = 0;
  for(value v = l; v != Val_emptylist; ) {
    c++;
    v = Field(v, 1);
  }
  return c;
}

CAMLprim value svm_problem_x_sparse_set_stub(value v_prob, value v_x)
{
  CAMLparam2(v_prob, v_x);
  svm_problem* prob = Svm_problem_val(v_prob);

  int m = Wosize_val(v_x);
  int* nz = (int*) calloc(m, sizeof(int));
  int nz_total = 0;
  for(int i = 0; i < m; i++) {
    int l = svm_list_length(Field(v_x, i));
    nz[i] += l;
    nz_total += l;
  }
  prob->x = (svm_node **) malloc(sizeof(svm_node*) * m);
  svm_node* buf = (svm_node*) malloc(sizeof(svm_node) * (nz_total + m));
  prob->x[0] = buf;
  for(int i = 1; i < m; i++)
    prob->x[i] = prob->x[i - 1] + nz[i-1] + 1;

  for(int i = 0; i < m; i++)
    copy_sparse_feature_vector(Field(v_x, i), prob->x[i]);

  free(nz);
  CAMLreturn(Val_unit);
}

CAMLprim value svm_problem_x_get_stub(value v_prob, value v_i, value v_j)
{
  CAMLparam3(v_prob, v_i, v_j);
  CAMLlocal1(v_result);

  svm_node **x = Svm_problem_val(v_prob)->x ;
  int i = Int_val(v_i),
      j = Int_val(v_j);

  v_result = caml_alloc(2, 0);
  Store_field(v_result, 0, Val_int(x[i][j].index));
  Store_field(v_result, 1, caml_copy_double(x[i][j].value));

  CAMLreturn(v_result);
}

CAMLprim value svm_problem_width_stub(value v_prob, value v_i)
{
  CAMLparam2(v_prob, v_i);
  CAMLlocal1(v_result);

  svm_node **x = Svm_problem_val(v_prob)->x ;
  int i = Int_val(v_i);
  int j = 0;
  while (x[i][j++].index != -1);

  CAMLreturn(Val_int(j-1));
}

CAMLprim value svm_problem_scale(value v_prob, value v_lower, value v_upper) {
  CAMLparam3(v_prob, v_lower, v_upper);
  svm_node **x = Svm_problem_val(v_prob)->x;
  int l = Svm_problem_val(v_prob)->l;
  int min_index = INT_MAX;
  int max_index = 0;
  double lower = Double_val(v_lower);
  double upper = Double_val(v_upper);

  for(int i = 0; i < l; i++)
    for(int j = 0; ; j++) {
      int index = x[i][j].index;
      if(index == -1) break;
      min_index = min(min_index, index);
      max_index = max(max_index, index);
    }
  assert(max_index >= 0);
  double* feature_max = (double*) malloc(((size_t) max_index + 1) * sizeof(double));
  double* feature_min = (double*) malloc(((size_t) max_index + 1) * sizeof(double));
  for(int i = 0; i <= max_index; i++) {
    feature_min[i] =   DBL_MAX;
    feature_max[i] = - DBL_MAX;
  }
  for(int i = 0; i < l; i++)
    for(int j = 0; ; j++) {
      int index = x[i][j].index;
      if(index == -1) break;
      feature_min[index] = min(feature_min[index], x[i][j].value);
      feature_max[index] = max(feature_max[index], x[i][j].value);
    }
  for(int i = 0; i < l; i++)
    for(int j = 0; ; j++) {
      int index = x[i][j].index;
      if(index == -1) break;
      x[i][j].value =
	lower +
	(upper - lower) * (x[i][j].value - feature_min[index])
	/ (feature_max[index] - feature_min[index]);
    }
  free(feature_min);
  free(feature_max);
  CAMLreturn(Val_unit);
}

CAMLprim value svm_problem_print_stub(value v_prob)
{
  CAMLparam1(v_prob);
  svm_problem* prob = Svm_problem_val(v_prob);

  svm_node** x = prob->x;
  double* y = prob->y;
  int l = prob->l;

  for (int i = 0; i < l; i++) {
    int j = 0;
    printf("%g ", y[i]);

    while (x[i][j].index != -1)
    {
      printf("(%d, %g) ", x[i][j].index, x[i][j].value);
      j++;
    }
    printf("\n");
  }
  CAMLreturn(Val_unit);
}

/* SVM Parameters */

static inline void destroy_param(svm_param *param)
{
  caml_stat_free(param->weight_label);
  caml_stat_free(param->weight);
}

void finalize_param_gc(value v_param)
{
  svm_param *param = Svm_param_val(v_param);
  caml_stat_free(param->weight_label);
  caml_stat_free(param->weight);
  caml_stat_free(param);
}

static inline value alloc_param(svm_param *param)
{
  value v_param = caml_alloc_final(2, finalize_param_gc, 1, 100);
  Svm_param_val(v_param) = param;
  return v_param;
}

CAMLprim value svm_param_create_stub(value v_param)
{
  CAMLparam1(v_param);
  CAMLlocal1(v_res);

  svm_param *param;
  param = Caml_stat_alloc(svm_param,1);

  param->svm_type = Long_val(Field(v_param, 0));
  param->kernel_type = Long_val(Field(v_param, 1));
  param->degree = Long_val(Field(v_param, 2));
  param->gamma = Double_val(Field(v_param, 3));
  param->coef0 = Double_val(Field(v_param, 4));
  param->C = Double_val(Field(v_param, 5));
  param->nu = Double_val(Field(v_param, 6));
  param->p = Double_val(Field(v_param, 7));
  param->cache_size = Double_val(Field(v_param, 8));
  param->eps = Double_val(Field(v_param, 9));
  param->shrinking = Bool_val(Field(v_param, 10));
  param->probability = Bool_val(Field(v_param, 11));
  param->nr_weight = Int_val(Field(v_param, 12));
  param->weight_label = NULL;
  param->weight = NULL;

  if (param->nr_weight)
  {
    value v_weight_label, v_weight;
    int i;

    v_weight_label = Field(v_param, 13);
    v_weight = Field(v_param, 14);

    param->weight_label = Caml_stat_alloc(int,param->nr_weight);
    param->weight = Caml_stat_alloc(double,param->nr_weight);

    for (i = 0; i < param->nr_weight; i++) // copy caml lists to C arrays
    {
      param->weight_label[i] = Int_val(Field(v_weight_label, 0));
      param->weight[i] = Double_val(Field(v_weight,0));
      v_weight_label = Field(v_weight_label, 1);
      v_weight = Field(v_weight, 1);
    }
  }

  CAMLreturn(alloc_param(param));
}

/* SVM Model */

void finalize_model_gc(value v_model)
{
  svm_model *model = Svm_model_val(v_model);
  svm_free_model_content(model);
}

static inline value alloc_model(svm_model *model)
{
  value v_model = caml_alloc_final(2, finalize_model_gc, 1, 100);
  Svm_model_val(v_model) = model;
  return v_model;
}

CAMLprim value svm_train_stub(value v_prob, value v_param)
{
  CAMLparam2(v_prob, v_param);
  CAMLlocal1(v_model);

  const char *error_msg;
  svm_prob *prob;
  svm_param *param;
  svm_model *model;

  prob = Svm_problem_val(v_prob);
  param = Svm_param_val(v_param);

  error_msg = svm_check_parameter(prob, param);
  if (error_msg)
    failwith(error_msg);
  model = svm_train(prob, param);
  v_model = alloc_model(model);

  CAMLreturn(v_model);
}

CAMLprim value svm_cross_validation_stub(value v_prob, value v_param,
                                         value v_n_fold)
{
  CAMLparam3(v_prob, v_param, v_n_fold);
  CAMLlocal1(v_target);

  const char *error_msg;
  svm_prob *prob;
  svm_param *param;
  int n_fold;
  double *target;

  prob = Svm_problem_val(v_prob);
  param = Svm_param_val(v_param);

  error_msg = svm_check_parameter(prob, param);
  if (error_msg)
    failwith(error_msg);

  n_fold = Long_val(v_n_fold);
  target = Caml_stat_alloc(double,prob->l);
  svm_cross_validation(prob, param, n_fold, target);

  v_target = alloc_bigarray_dims(BIGARRAY_FLOAT64 | BIGARRAY_FORTRAN_LAYOUT,
                                 1, target, prob->l);
  CAMLreturn(v_target);
}

CAMLprim value svm_save_model_stub(value v_filename, value v_model)
{
  CAMLparam2(v_filename, v_model);
  if (svm_save_model(String_val(v_filename), Svm_model_val(v_model)) == -1)
    failwith("Could not save model");
  CAMLreturn(Val_unit);
}

CAMLprim value svm_load_model_stub(value v_filename)
{
  CAMLparam1(v_filename);
  CAMLlocal1(v_model);

  svm_model *model = svm_load_model(String_val(v_filename));
  if (model == NULL)
    failwith("Could not load model.");
  v_model = alloc_model(model);

  CAMLreturn(v_model);
}

CAMLprim value svm_get_svm_type_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(v_type);
  v_type = Val_long(svm_get_svm_type(Svm_model_val(v_model)));
  CAMLreturn(v_type);
}

CAMLprim value svm_get_kernel_type_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(v_kernel);
  v_kernel = Val_long(Svm_model_val(v_model)->param.kernel_type);
  CAMLreturn(v_kernel);
}

CAMLprim value svm_get_nr_class_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(v_nr_class);
  v_nr_class = Val_long(svm_get_nr_class(Svm_model_val(v_model)));
  CAMLreturn(v_nr_class);
}

CAMLprim value svm_get_labels_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal2(v_labels, v_cons);
  int nr_class, *labels, i;

  nr_class = svm_get_nr_class(Svm_model_val(v_model));
  labels = Caml_stat_alloc(int,nr_class);
  svm_get_labels(Svm_model_val(v_model), labels);

  v_labels = Val_emptylist;
  for (i = nr_class-1; i >= 0; i--)
  {
    v_cons = caml_alloc(2, 0);
    Store_field(v_cons, 0, Val_int(labels[i]));
    Store_field(v_cons, 1, v_labels);
    v_labels = v_cons;
  }
  caml_stat_free(labels);
  CAMLreturn(v_labels);
}

CAMLprim value svm_get_nr_sv_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLreturn(Val_long(Svm_model_val(v_model)->l));
}

CAMLprim value svm_get_svr_probability_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(v_res);
  v_res = caml_copy_double(svm_get_svr_probability(Svm_model_val(v_model)));
  CAMLreturn(v_res);
}

svm_node* create_feature_vector_sparse(value v_l) {
  int n = svm_list_length(v_l);
  svm_node* dst = (svm_node*) malloc(sizeof(svm_node) * (n + 1));
  copy_sparse_feature_vector(v_l, dst);
  return dst;
}

CAMLprim value svm_predict_values_sparse(value v_model, value v_x)
{
  CAMLparam2(v_model, v_x);
  CAMLlocal1(v_decvals);

  int nr_class, nr_decvals, i;
  double *decvals;

  nr_class = svm_get_nr_class(Svm_model_val(v_model));
  nr_decvals = (nr_class*(nr_class-1))/2;
  decvals = Caml_stat_alloc(double, nr_decvals);

  svm_node* x = create_feature_vector_sparse(v_x);
  svm_predict_values(Svm_model_val(v_model), x, decvals);

  v_decvals = caml_alloc(nr_class * Double_wosize, Double_array_tag);
  for (i = 0; i < nr_class; i++)
  {
    Store_double_field(v_decvals, i, decvals[i]);
  }

  free(x);
  caml_stat_free(decvals);
  CAMLreturn(v_decvals);
}

CAMLprim value svm_predict_sparse(value v_model, value v_list)
{
  CAMLparam2(v_model, v_list);
  CAMLlocal1(v_label);
  double label;

  svm_node* x = create_feature_vector_sparse(v_list);
  label = svm_predict(Svm_model_val(v_model), x);
  v_label = caml_copy_double(label);
  free(x);

  CAMLreturn(v_label);
}

CAMLprim value svm_check_probability_model_stub(value v_model)
{
  CAMLparam1(v_model);
  CAMLlocal1(v_res);
  v_res = Val_int(svm_check_probability_model(Svm_model_val(v_model)));
  CAMLreturn(v_res);
}

CAMLprim value svm_predict_probability_sparse(value v_model, value v_list)
{
  CAMLparam2(v_model, v_list);
  CAMLlocal3(v_label, v_estimates, v_result);

  int nr_class, i;
  double label, *estimates;

  nr_class = svm_get_nr_class(Svm_model_val(v_model));
  estimates = Caml_stat_alloc(double, nr_class);

  svm_node* x = create_feature_vector_sparse(v_list);
  label = svm_predict_probability(Svm_model_val(v_model), x, estimates);
  free(x);

  v_result = caml_alloc(2, 0);
  v_label = caml_copy_double(label);

  v_estimates = caml_alloc(nr_class * Double_wosize, Double_array_tag);
  for (i = 0; i < nr_class; i++)
  {
    Store_double_field(v_estimates, i, estimates[i]);
  }

  Store_field(v_result, 0, v_label);
  Store_field(v_result, 1, v_estimates);

  caml_stat_free(estimates);
  CAMLreturn(v_result);
}

}
