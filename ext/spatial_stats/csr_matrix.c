#include <ruby.h>
#include <stdlib.h>
#include <stdio.h>
#include "csr_matrix.h"

void csr_matrix_free(void *mat)
{
    csr_matrix *csr = (csr_matrix *)mat;

    if (csr->init == 1)
    {
        free(csr->values);
        free(csr->col_index);
        free(csr->row_index);
    }
    free(mat);
}

size_t csr_matrix_memsize(const void *ptr)
{
    const csr_matrix *csr = (const csr_matrix *)ptr;
    return sizeof(*csr);
}

const rb_data_type_t csr_matrix_type = {
    "SpatialStats::Weights::CSRMatrix",
    {NULL, csr_matrix_free, csr_matrix_memsize},
    0,
    0,
    RUBY_TYPED_FREE_IMMEDIATELY
};

VALUE csr_matrix_alloc(VALUE self)
{
    csr_matrix *csr = ALLOC(csr_matrix);
    return TypedData_Wrap_Struct(self, &csr_matrix_type, csr);
}

void mat_to_sparse(csr_matrix *csr, VALUE data, VALUE keys, VALUE num_rows)
{

    int nnz = 0;
    int n = NUM2INT(num_rows);
    int m;

    VALUE key;
    VALUE row;
    VALUE entry;
    VALUE key_lookup = rb_hash_new();
    VALUE weight_sym = ID2SYM(rb_intern("weight"));
    VALUE id_sym = ID2SYM(rb_intern("id"));

    double *values;
    int *col_index;
    int *row_index;

    int nz_idx;
    double weight;

    int i;
    int j;

    // first get number non zero count so we can alloc values and col_index
    for (i = 0; i < n; i++)
    {
        key = rb_ary_entry(keys, i);

        // set lookup index for this key
        rb_hash_aset(key_lookup, key, INT2NUM(i));

        // check the value of this row is actually an array
        // if it is, add array len to nnz
        row = rb_hash_aref(data, key);
        Check_Type(row, T_ARRAY);
        nnz += (int)RARRAY_LEN(row); // Explicit cast to int
    }

    values = malloc(sizeof(double) * nnz);
    col_index = malloc(sizeof(int) * nnz);
    row_index = malloc(sizeof(int) * (n + 1));

    // for every row, work through each hash
    // in each hash, add the weight to values and get col_index
    // by looking at the key_lookup of id.
    // Row index will be computed by adding len of each row and updating array.
    nz_idx = 0;
    for (i = 0; i < n; i++)
    {
        row_index[i] = nz_idx;

        key = rb_ary_entry(keys, i);
        row = rb_hash_aref(data, key);
        m = (int)RARRAY_LEN(row); // Explicit cast to int

        for (j = 0; j < m; j++)
        {
            entry = rb_ary_entry(row, j);
            Check_Type(entry, T_HASH);

            key = rb_hash_aref(entry, id_sym);
            weight = NUM2DBL(rb_hash_aref(entry, weight_sym));

            // assign the nnz the weight
            // get index in the keys array of key from lookup table
            values[nz_idx] = weight;
            col_index[nz_idx] = NUM2INT(rb_hash_aref(key_lookup, key));
            nz_idx++;
        }
    }
    row_index[n] = nnz;

    csr->n = n;
    csr->nnz = nnz;
    csr->values = values;
    csr->col_index = col_index;
    csr->row_index = row_index;
    csr->init = 1;
}

/**
 *  A new instance of CSRMatrix.
 *  Uses a Dictionary of Keys (DOK) as input to represent a square matrix.
 *  @example
 *      weights = {
 *          'a' => [{ id: 'c', weight: 1 }],
 *          'b' => [{ id: 'b', weight: 1 }],
 *          'c' => [{ id: 'a', weight: 1 }]
 *      }
 *      num_rows = 3
 *      
 *      csr = CSRMatrix.new(data, num_rows)
 * 
 *  @param [Array] data in 1-D format
 *  @param [Integer] num_rows in the 2-D representation
 *  
 *  @return [CSRMatrix]
 */
VALUE csr_matrix_initialize(VALUE self, VALUE data, VALUE num_rows)
{
    VALUE keys;
    csr_matrix *csr;
    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);
    csr->init = 0;

    Check_Type(data, T_HASH);
    Check_Type(num_rows, T_FIXNUM);

    keys = rb_funcall(data, rb_intern("keys"), 0);

    // check dimensions are correct
    if (NUM2INT(num_rows) != (int)RARRAY_LEN(keys)) // Explicit cast to int
    {
        rb_raise(rb_eArgError, "n_rows != keys.size, check your dimensions");
    }

    mat_to_sparse(csr, data, keys, num_rows);

    rb_iv_set(self, "@n", num_rows);
    rb_iv_set(self, "@nnz", INT2NUM(csr->nnz));

    return self;
}

/**
 *  Non-zero values in the matrix.
 *  
 *  @return [Array] of the non-zero values.
 */
VALUE csr_matrix_values(VALUE self)
{
    csr_matrix *csr;
    VALUE result;

    int i;

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    result = rb_ary_new_capa(csr->nnz);
    for (i = 0; i < csr->nnz; i++)
    {
        rb_ary_store(result, i, DBL2NUM(csr->values[i]));
    }

    return result;
}

/**
 *  Column indices of the non-zero values.
 *  
 *  @return [Array] of the column indices.
 */
VALUE csr_matrix_col_index(VALUE self)
{
    csr_matrix *csr;
    VALUE result;

    int i;

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    result = rb_ary_new_capa(csr->nnz);
    for (i = 0; i < csr->nnz; i++)
    {
        rb_ary_store(result, i, INT2NUM(csr->col_index[i]));
    }

    return result;
}

/**
 *  Row indices of the non-zero values. Represents the start index
 *  of values in a row. For example [0,2,3] would represent a matrix
 *  with 2 rows, the first containing 2 non-zero values and the second
 *  containing 1. Length is num_rows + 1.
 * 
 *  Used for row slicing operations.
 *  
 *  @return [Array] of the row indices.
 */
VALUE csr_matrix_row_index(VALUE self)
{
    csr_matrix *csr;
    VALUE result;

    int i;

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    result = rb_ary_new_capa(csr->n + 1);
    for (i = 0; i <= csr->n; i++)
    {
        rb_ary_store(result, i, INT2NUM(csr->row_index[i]));
    }

    return result;
}

/**
 *  Multiply matrix by the input vector.
 *  
 *  @see https://github.com/scipy/scipy/blob/53fac7a1d8a81d48be757632ad285b6fc76529ba/scipy/sparse/sparsetools/csr.h#L1120
 *  
 *  @param [Array] vec of length n. 
 * 
 *  @return [Array] of the result of the multiplication.
 */
VALUE csr_matrix_mulvec(VALUE self, VALUE vec)
{
    csr_matrix *csr;
    VALUE result;

    int i;
    int jj;
    double tmp;

    Check_Type(vec, T_ARRAY);

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    if (RARRAY_LEN(vec) != csr->n)
    {
        rb_raise(rb_eArgError, "Dimension Mismatch CSRMatrix.n != vec.size");
    }

    result = rb_ary_new_capa(csr->n);

    // float *vals = (float *)DATA_PTR(result);

    for (i = 0; i < csr->n; i++)
    {
        tmp = 0;
        for (jj = csr->row_index[i]; jj < csr->row_index[i + 1]; jj++)
        {
            tmp += csr->values[jj] * NUM2DBL(rb_ary_entry(vec, csr->col_index[jj]));
        }
        rb_ary_store(result, i, DBL2NUM(tmp));
    }

    return result;
}

/**
 *  Compute the dot product of the given row with the input vector.
 *  Equivalent to +mulvec(vec)[row]+.
 *  
 *  @param [Array] vec of length n. 
 *  @param [Integer] row of the dot product.
 * 
 *  @return [Float] of the result of the dot product.
 */
VALUE csr_matrix_dot_row(VALUE self, VALUE vec, VALUE row)
{
    csr_matrix *csr;
    VALUE result;

    int i;
    int jj;
    double tmp;

    Check_Type(vec, T_ARRAY);
    Check_Type(row, T_FIXNUM);

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    if (RARRAY_LEN(vec) != csr->n)
    {
        rb_raise(rb_eArgError, "Dimension Mismatch CSRMatrix.n != vec.size");
    }

    i = NUM2INT(row);
    if (!(i >= 0 && i < csr->n))
    {
        rb_raise(rb_eArgError, "Index Error row_idx >= m or idx < 0");
    }

    tmp = 0;
    for (jj = csr->row_index[i]; jj < csr->row_index[i + 1]; jj++)
    {
        tmp += csr->values[jj] * NUM2DBL(rb_ary_entry(vec, csr->col_index[jj]));
    }

    result = DBL2NUM(tmp);
    return result;
}

/** 
 *  A hash representation of the matrix with coordinates as keys.
 *  @example
 *      data = [
 *              [0, 1, 0]
 *              [0, 0, 0],
 *              [1, 0, 1]   
 *             ]
 *      num_rows = 3
 *      num_cols = 3
 *      data = data.flatten!
 *      csr = CSRMatrix.new(data, num_rows, num_cols)
 *  
 *      csr.coordinates
 *      # => {
 *              [0,1] => 1,
 *              [2,0] => 1,
 *              [2,2] => 1
 *           }
 *  
 *  @return [Hash]
 */
VALUE csr_matrix_coordinates(VALUE self)
{
    csr_matrix *csr;
    VALUE result;

    int i;
    int k;

    VALUE key;
    VALUE val;
    int row_end;

    TypedData_Get_Struct(self, csr_matrix, &csr_matrix_type, csr);

    result = rb_hash_new();

    // iterate through every value in the matrix and assign it's coordinates
    // [x,y] as the key to the hash, with the value as the value.
    // Use i to keep track of what row we are on.
    i = 0;
    row_end = csr->row_index[1];
    for (k = 0; k < csr->nnz; k++)
    {
        if (k == row_end)
        {
            i++;
            row_end = csr->row_index[i + 1];
        }

        // store i,j coordinates j is col_index[k]
        key = rb_ary_new_capa(2);
        rb_ary_store(key, 0, INT2NUM(i));
        rb_ary_store(key, 1, INT2NUM(csr->col_index[k]));

        val = DBL2NUM(csr->values[k]);

        rb_hash_aset(result, key, val);
    }

    return result;
}