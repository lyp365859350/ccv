#include "case.h"
#include "ccv_case.h"
#include "ccv_nnc_case.h"
#include <ccv.h>
#include <nnc/ccv_nnc.h>
#include <nnc/ccv_nnc_easy.h>
#include <3rdparty/dsfmt/dSFMT.h>

TEST_SETUP()
{
	ccv_nnc_init();
}

#define INPUT_DIM (3)
#define OUTPUT_DIM (96)

#define INPUT_SIZE (224)
#define OUTPUT_SIZE (112)

#define KERNEL_SIZE (7)

#define BATCH_SIZE (64)

TEST_CASE("cudnn forward convolution")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_CONVOLUTION_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_t cmd = CMD_CONVOLUTION_FORWARD(1, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM);
	cmd.backend = CCV_NNC_BACKEND_CPU_REF;
	assert(cmd.backend >= 0);
	ccv_nnc_hint_t hint = ccv_nnc_hint_auto(cmd.info, a->info, b->info);
	assert(ccv_nnc_hint_verify(hint, cmd.info, a->info, b->info) == 0);
	ccv_nnc_tensor_t* w = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* bias = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM), 0);
	// configure the inlets.
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE * OUTPUT_DIM; i++)
		w->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) / (INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE);
	for (i = 0; i < INPUT_SIZE * INPUT_SIZE * INPUT_DIM * ccv_max(1, BATCH_SIZE); i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < OUTPUT_DIM; i++)
		bias->data.f32[i] = (float)i / OUTPUT_DIM;
	// Copy generated matrix values over to GPU.
	ccv_nnc_tensor_t* ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* gw = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* gwo = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, OUTPUT_DIM, INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE), 0);
	ccv_nnc_tensor_t* gbias = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, OUTPUT_DIM), 0);
	ccv_nnc_cmd_t move = CMD_DATA_TRANSFER_FORWARD();
	move.backend = CCV_NNC_BACKEND_GPU_REF;
	assert(move.backend >= 0);
	ccv_nnc_cmd_exec(move, ccv_nnc_no_hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(ga, gw, gbias), 0);
	ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(b), 0);
	ccv_nnc_tensor_t* gc = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);

	ccv_nnc_cmd_t transform = CMD_FORMAT_TRANSFORM_FORWARD();
	transform.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(transform.backend >= 0);
	ccv_nnc_stream_context_t* stream_context = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	ccv_nnc_cmd_exec(transform, ccv_nnc_no_hint, 0, TENSOR_LIST(gw), TENSOR_LIST(gwo), stream_context);
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_tensor_free(gw);

	cmd.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(cmd.backend >= 0);
	cmd.algorithm = -1;
	cmd = ccv_nnc_cmd_autotune(cmd, 1 * 1024 * 1024 * 1024, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context);
	assert(CCV_NNC_EXEC_SUCCESS == ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context));
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_stream_context_free(stream_context);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_exec(move, ccv_nnc_no_hint, 0, TENSOR_LIST(gc), TENSOR_LIST(c), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, b->data.f32, c->data.f32, OUTPUT_DIM * OUTPUT_SIZE * OUTPUT_SIZE, 1e-5, "output from cudnn should match from CPU");
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(gc);
	ccv_nnc_tensor_free(bias);
	ccv_nnc_tensor_free(w);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(gbias);
	ccv_nnc_tensor_free(gwo);
	ccv_nnc_tensor_free(ga);
}

TEST_CASE("cudnn forward convolution in nchw format")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_CONVOLUTION_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_t cmd = CMD_CONVOLUTION_FORWARD(1, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM);
	cmd.backend = CCV_NNC_BACKEND_CPU_REF;
	assert(cmd.backend >= 0);
	ccv_nnc_hint_t hint = ccv_nnc_hint_auto(cmd.info, a->info, b->info);
	assert(ccv_nnc_hint_verify(hint, cmd.info, a->info, b->info) == 0);
	ccv_nnc_tensor_t* w = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* bias = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM), 0);
	// configure the inlets.
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i, j, k;
	for (i = 0; i < INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE * OUTPUT_DIM; i++)
		w->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) / (INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE);
	for (i = 0; i < INPUT_SIZE * INPUT_SIZE * INPUT_DIM * ccv_max(1, BATCH_SIZE); i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* ao = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, BATCH_SIZE, INPUT_DIM, INPUT_SIZE, INPUT_SIZE), 0);
	for (i = 0; i < BATCH_SIZE; i++)
		for (j = 0; j < INPUT_DIM; j++)
			for (k = 0; k < INPUT_SIZE * INPUT_SIZE; k++)
				ao->data.f32[i * INPUT_DIM * INPUT_SIZE * INPUT_SIZE + j * INPUT_SIZE * INPUT_SIZE + k] = a->data.f32[i * INPUT_SIZE * INPUT_SIZE * INPUT_DIM + k * INPUT_DIM + j];
	for (i = 0; i < OUTPUT_DIM; i++)
		bias->data.f32[i] = (float)i / OUTPUT_DIM;
	// Copy generated matrix values over to GPU.
	ccv_nnc_tensor_t* ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, BATCH_SIZE, INPUT_DIM, INPUT_SIZE, INPUT_SIZE), 0);
	ccv_nnc_tensor_t* gw = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* gwo = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, OUTPUT_DIM, INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE), 0);
	ccv_nnc_tensor_t* gbias = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, OUTPUT_DIM), 0);
	ccv_nnc_cmd_t move = CMD_DATA_TRANSFER_FORWARD();
	move.backend = CCV_NNC_BACKEND_GPU_REF;
	assert(move.backend >= 0);
	ccv_nnc_cmd_exec(move, ccv_nnc_no_hint, 0, TENSOR_LIST(ao, w, bias), TENSOR_LIST(ga, gw, gbias), 0);
	ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(b), 0);
	ccv_nnc_tensor_t* gc = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, BATCH_SIZE, OUTPUT_DIM, OUTPUT_SIZE, OUTPUT_SIZE), 0);

	ccv_nnc_cmd_t transform = CMD_FORMAT_TRANSFORM_FORWARD();
	transform.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(transform.backend >= 0);
	ccv_nnc_stream_context_t* stream_context = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	ccv_nnc_cmd_exec(transform, ccv_nnc_no_hint, 0, TENSOR_LIST(gw), TENSOR_LIST(gwo), stream_context);
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_tensor_free(gw);

	cmd.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(cmd.backend >= 0);
	cmd.algorithm = -1;
	cmd = ccv_nnc_cmd_autotune(cmd, 1 * 1024 * 1024 * 1024, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context);
	assert(CCV_NNC_EXEC_SUCCESS == ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context));
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_stream_context_free(stream_context);
	ccv_nnc_tensor_t* co = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, BATCH_SIZE, OUTPUT_DIM, OUTPUT_SIZE, OUTPUT_SIZE), 0);
	ccv_nnc_cmd_exec(move, ccv_nnc_no_hint, 0, TENSOR_LIST(gc), TENSOR_LIST(co), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	for (i = 0; i < BATCH_SIZE; i++)
		for (j = 0; j < OUTPUT_DIM; j++)
			for (k = 0; k < OUTPUT_SIZE * OUTPUT_SIZE; k++)
				c->data.f32[i * OUTPUT_SIZE * OUTPUT_SIZE * OUTPUT_DIM + k * OUTPUT_DIM + j] = co->data.f32[i * OUTPUT_DIM * OUTPUT_SIZE * OUTPUT_SIZE + j * OUTPUT_SIZE * OUTPUT_SIZE + k];
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, b->data.f32, c->data.f32, OUTPUT_DIM * OUTPUT_SIZE * OUTPUT_SIZE, 1e-5, "output from cudnn should match from CPU");
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(gc);
	ccv_nnc_tensor_free(bias);
	ccv_nnc_tensor_free(w);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(gbias);
	ccv_nnc_tensor_free(gwo);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(ao);
	ccv_nnc_tensor_free(co);
}

TEST_CASE("cudnn forward convolution in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_CONVOLUTION_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_t cmd = CMD_CONVOLUTION_FORWARD(1, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM);
	cmd.backend = CCV_NNC_BACKEND_CPU_REF;
	assert(cmd.backend >= 0);
	ccv_nnc_hint_t hint = ccv_nnc_hint_auto(cmd.info, a->info, b->info);
	assert(ccv_nnc_hint_verify(hint, cmd.info, a->info, b->info) == 0);
	ccv_nnc_tensor_t* w = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* bias = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, OUTPUT_DIM), 0);
	// configure the inlets.
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE * OUTPUT_DIM; i++)
		w->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) / (INPUT_DIM * KERNEL_SIZE * KERNEL_SIZE);
	for (i = 0; i < INPUT_SIZE * INPUT_SIZE * INPUT_DIM * ccv_max(1, BATCH_SIZE); i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < OUTPUT_DIM; i++)
		bias->data.f32[i] = (float)i / OUTPUT_DIM;
	ccv_nnc_tensor_t* a1 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* w1 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* bias1 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, OUTPUT_DIM), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(a1, w1, bias1), 0);
	// Copy generated matrix values over to GPU.
	ccv_nnc_tensor_t* ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, BATCH_SIZE, INPUT_SIZE, INPUT_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* gw = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, OUTPUT_DIM, KERNEL_SIZE, KERNEL_SIZE, INPUT_DIM), 0);
	ccv_nnc_tensor_t* gwo = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 16F, OUTPUT_DIM, INPUT_DIM, KERNEL_SIZE, KERNEL_SIZE), 0);
	ccv_nnc_tensor_t* gbias = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, OUTPUT_DIM), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a1, w1, bias1), TENSOR_LIST(ga, gw, gbias), 0);
	ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(a, w, bias), TENSOR_LIST(b), 0);
	ccv_nnc_tensor_t* gc = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);

	ccv_nnc_cmd_t transform = CMD_FORMAT_TRANSFORM_FORWARD();
	transform.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(transform.backend >= 0);
	ccv_nnc_stream_context_t* stream_context = ccv_nnc_stream_context_new(CCV_STREAM_CONTEXT_GPU);
	ccv_nnc_cmd_exec(transform, ccv_nnc_no_hint, 0, TENSOR_LIST(gw), TENSOR_LIST(gwo), stream_context);
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_tensor_free(gw);

	cmd.backend = CCV_NNC_BACKEND_GPU_CUDNN;
	assert(cmd.backend >= 0);
	cmd.algorithm = -1;
	cmd = ccv_nnc_cmd_autotune(cmd, 1 * 1024 * 1024 * 1024, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context);
	assert(CCV_NNC_EXEC_SUCCESS == ccv_nnc_cmd_exec(cmd, hint, 0, TENSOR_LIST(ga, gwo, gbias), TENSOR_LIST(gc), stream_context));
	ccv_nnc_stream_context_wait(stream_context);
	ccv_nnc_stream_context_free(stream_context);
	ccv_nnc_tensor_t* c1 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gc), TENSOR_LIST(c1), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, BATCH_SIZE, OUTPUT_SIZE, OUTPUT_SIZE, OUTPUT_DIM), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(c1), TENSOR_LIST(c), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, b->data.f32, c->data.f32, OUTPUT_DIM * OUTPUT_SIZE * OUTPUT_SIZE, 5e-3, "output from cudnn should match from CPU");
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(gc);
	ccv_nnc_tensor_free(bias);
	ccv_nnc_tensor_free(w);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(c1);
	ccv_nnc_tensor_free(bias1);
	ccv_nnc_tensor_free(w1);
	ccv_nnc_tensor_free(a1);
	ccv_nnc_tensor_free(gbias);
	ccv_nnc_tensor_free(gwo);
	ccv_nnc_tensor_free(ga);
}

TEST_CASE("compare batch norm with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		(ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_REF) || ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN)));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "host x");
	ccv_nnc_tensor_symbol_t bx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t by = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "host y");
	ccv_nnc_tensor_symbol_t scale = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "scale");
	ccv_nnc_tensor_symbol_t bias = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "bias");
	ccv_nnc_tensor_symbol_t bmean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t bmean_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_t saved_mean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t saved_inv_std = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(bx), "transfer x");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(scale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(bias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(bx, scale, bias, bmean, bvar), TENSOR_SYMBOL_LIST(by, bmean_out, bvar_out, saved_mean, saved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(by), TENSOR_SYMBOL_LIST(y), "transfer y");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	float xdata[2 * 2 * 2 * 10];
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	int i;
	dsfmt_init_gen_rand(&dsfmt, 1);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		x_tensor->data.f32[i] = xdata[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_symbolic_graph_t* const cpu_symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t cx = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t cy = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t cscale = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "scale");
	ccv_nnc_tensor_symbol_t cbias = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "bias");
	ccv_nnc_tensor_symbol_t cmean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t cmean_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_t csaved_mean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t csaved_inv_std = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(cscale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(cbias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(cx, cscale, cbias, cmean, cvar), TENSOR_SYMBOL_LIST(cy, cmean_out, cvar_out, csaved_mean, csaved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* cpu_graph = 0;
	ccv_nnc_tensor_arena_t* cpu_tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* cpu_graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(cpu_symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph), &cpu_graph, &cpu_tensor_arena, &cpu_graph_exec_arena);
	ccv_nnc_tensor_t* const cx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cx);
	memcpy(cx_tensor->data.f32, xdata, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_graph_run(cpu_graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const cy_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cy);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cy_tensor->data.f32, 2 * 2 * 2 * 10, 1e-5, "batch norm result from cudnn should match the one from reference implementation");
	ccv_nnc_symbolic_graph_free(cpu_symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_tensor_arena_free(cpu_tensor_arena);
	ccv_nnc_graph_exec_arena_free(cpu_graph_exec_arena);
	ccv_nnc_graph_free(cpu_graph);
}

TEST_CASE("compare batch norm with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		(ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_REF) || ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN)));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "host x");
	ccv_nnc_tensor_symbol_t x16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 2, 2, 2, 10), "x in half precision");
	ccv_nnc_tensor_symbol_t bx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t by = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t y16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 2, 2, 2, 10), "y in half precision");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "host y");
	ccv_nnc_tensor_symbol_t scale = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "scale");
	ccv_nnc_tensor_symbol_t bias = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "bias");
	ccv_nnc_tensor_symbol_t bmean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t bmean_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_t saved_mean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t saved_inv_std = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATATYPE_CONVERSION_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(x16), "convert x");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x16), TENSOR_SYMBOL_LIST(bx), "transfer x");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(scale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(bias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(bx, scale, bias, bmean, bvar), TENSOR_SYMBOL_LIST(by, bmean_out, bvar_out, saved_mean, saved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(by), TENSOR_SYMBOL_LIST(y16), "transfer y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATATYPE_CONVERSION_FORWARD(), TENSOR_SYMBOL_LIST(y16), TENSOR_SYMBOL_LIST(y), "convert y");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	float xdata[2 * 2 * 2 * 10];
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	int i;
	dsfmt_init_gen_rand(&dsfmt, 1);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		x_tensor->data.f32[i] = xdata[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_symbolic_graph_t* const cpu_symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t cx = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t cy = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t cscale = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "scale");
	ccv_nnc_tensor_symbol_t cbias = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "bias");
	ccv_nnc_tensor_symbol_t cmean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t cmean_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_t csaved_mean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t csaved_inv_std = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(cscale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(cbias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(cx, cscale, cbias, cmean, cvar), TENSOR_SYMBOL_LIST(cy, cmean_out, cvar_out, csaved_mean, csaved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* cpu_graph = 0;
	ccv_nnc_tensor_arena_t* cpu_tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* cpu_graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(cpu_symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph), &cpu_graph, &cpu_tensor_arena, &cpu_graph_exec_arena);
	ccv_nnc_tensor_t* const cx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cx);
	memcpy(cx_tensor->data.f32, xdata, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_graph_run(cpu_graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const cy_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cy);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cy_tensor->data.f32, 2 * 2 * 2 * 10, 1e-3, "batch norm result from cudnn should match the one from reference implementation");
	ccv_nnc_symbolic_graph_free(cpu_symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_tensor_arena_free(cpu_tensor_arena);
	ccv_nnc_graph_exec_arena_free(cpu_graph_exec_arena);
	ccv_nnc_graph_free(cpu_graph);
}

TEST_CASE("compare batch norm gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		(ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_REF) || ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN)));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t bx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t by = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t scale = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "scale");
	ccv_nnc_tensor_symbol_t bias = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "bias");
	ccv_nnc_tensor_symbol_t bmean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t bmean_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_t saved_mean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t saved_inv_std = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(scale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(bias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(bx, scale, bias, bmean, bvar), TENSOR_SYMBOL_LIST(by, bmean_out, bvar_out, saved_mean, saved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(by), TENSOR_SYMBOL_LIST(bx, scale, bias), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_tensor_symbol_t dby = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, by);
	ccv_nnc_tensor_symbol_t dbx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, bx);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const bx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, bx);
	dsfmt_t dsfmt;
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	int i;
	dsfmt_init_gen_rand(&dsfmt, 1);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(bx_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	ccv_nnc_tensor_t* const dby_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dby);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dby_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dbx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dbx);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dbx_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_symbolic_graph_t* const cpu_symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t cx = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t cy = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t cscale = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "scale");
	ccv_nnc_tensor_symbol_t cbias = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "bias");
	ccv_nnc_tensor_symbol_t cmean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t cmean_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_t csaved_mean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t csaved_inv_std = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(cscale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(cbias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(cx, cscale, cbias, cmean, cvar), TENSOR_SYMBOL_LIST(cy, cmean_out, cvar_out, csaved_mean, csaved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(cpu_symbolic_graph, TENSOR_SYMBOL_LIST(cy), TENSOR_SYMBOL_LIST(cx, cscale, cbias), SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_tensor_symbol_t dcy = ccv_nnc_tensor_symbol_for_backward(cpu_symbolic_graph, cy);
	ccv_nnc_tensor_symbol_t dcx = ccv_nnc_tensor_symbol_for_backward(cpu_symbolic_graph, cx);
	ccv_nnc_graph_t* cpu_graph = 0;
	ccv_nnc_tensor_arena_t* cpu_tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* cpu_graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(cpu_symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph), &cpu_graph, &cpu_tensor_arena, &cpu_graph_exec_arena);
	ccv_nnc_tensor_t* const cx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cx);
	memcpy(cx_tensor->data.f32, x_tensor->data.f32, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_tensor_t* const dcy_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, dcy);
	memcpy(dcy_tensor->data.f32, dy_tensor->data.f32, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_graph_run(cpu_graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dcx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, dcx);
	REQUIRE_TENSOR_EQ(dx_tensor, dcx_tensor, "batch norm gradient result from cudnn should match the one from reference implementation");
	ccv_nnc_symbolic_graph_free(cpu_symbolic_graph);
	ccv_nnc_tensor_arena_free(cpu_tensor_arena);
	ccv_nnc_graph_exec_arena_free(cpu_graph_exec_arena);
	ccv_nnc_graph_free(cpu_graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dx_tensor);
}

TEST_CASE("compare batch norm gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_BATCH_NORM_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		(ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_REF) || ccv_nnc_cmd_ok(CCV_NNC_SET_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN)));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t bx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t by = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t scale = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "scale");
	ccv_nnc_tensor_symbol_t bias = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "bias");
	ccv_nnc_tensor_symbol_t bmean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(symbolic_graph, bvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t bmean_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "mean");
	ccv_nnc_tensor_symbol_t bvar_out = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "var");
	ccv_nnc_tensor_symbol_t saved_mean = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t saved_inv_std = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(scale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(bias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(bx, scale, bias, bmean, bvar), TENSOR_SYMBOL_LIST(by, bmean_out, bvar_out, saved_mean, saved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(by), TENSOR_SYMBOL_LIST(bx, scale, bias), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_tensor_symbol_t dby = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, by);
	ccv_nnc_tensor_symbol_t dbx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, bx);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const bx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, bx);
	dsfmt_t dsfmt;
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 2, 2, 2, 10), 0);
	int i;
	dsfmt_init_gen_rand(&dsfmt, 1);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(bx_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 2, 2, 2, 10), 0);
	ccv_nnc_tensor_t* const dby_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dby);
	for (i = 0; i < 2 * 2 * 2 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dby_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dbx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dbx);
	ccv_nnc_tensor_t* const dx16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 2, 2, 2, 10), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dbx_tensor), TENSOR_LIST(dx16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dx16_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_symbolic_graph_t* const cpu_symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t cx = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "x");
	ccv_nnc_tensor_symbol_t cy = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 2, 2, 2, 10), "y");
	ccv_nnc_tensor_symbol_t cscale = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "scale");
	ccv_nnc_tensor_symbol_t cbias = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "bias");
	ccv_nnc_tensor_symbol_t cmean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cmean, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_set_flags(cpu_symbolic_graph, cvar, CCV_NNC_TENSOR_SYMBOL_INIT_ZEROS);
	ccv_nnc_tensor_symbol_t cmean_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "mean");
	ccv_nnc_tensor_symbol_t cvar_out = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "var");
	ccv_nnc_tensor_symbol_t csaved_mean = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_mean");
	ccv_nnc_tensor_symbol_t csaved_inv_std = ccv_nnc_tensor_symbol_new(cpu_symbolic_graph, CPU_TENSOR_NHWC(32F, 10), "saved_inv_std");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(1), 0, 0, TENSOR_SYMBOL_LIST(cscale), "set_scale");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_SET_FORWARD(0), 0, 0, TENSOR_SYMBOL_LIST(cbias), "set_bias");
	ccv_nnc_graph_exec_symbol_new(cpu_symbolic_graph, CMD_BATCH_NORM_FORWARD(1e-4, 0, 0.9, 0, 1, 2), TENSOR_SYMBOL_LIST(cx, cscale, cbias, cmean, cvar), TENSOR_SYMBOL_LIST(cy, cmean_out, cvar_out, csaved_mean, csaved_inv_std), "batch_norm");
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(cpu_symbolic_graph, TENSOR_SYMBOL_LIST(cy), TENSOR_SYMBOL_LIST(cx, cscale, cbias), SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(cpu_symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_tensor_symbol_t dcy = ccv_nnc_tensor_symbol_for_backward(cpu_symbolic_graph, cy);
	ccv_nnc_tensor_symbol_t dcx = ccv_nnc_tensor_symbol_for_backward(cpu_symbolic_graph, cx);
	ccv_nnc_graph_t* cpu_graph = 0;
	ccv_nnc_tensor_arena_t* cpu_tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* cpu_graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(cpu_symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(cpu_symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(cpu_symbolic_graph), &cpu_graph, &cpu_tensor_arena, &cpu_graph_exec_arena);
	ccv_nnc_tensor_t* const cx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, cx);
	memcpy(cx_tensor->data.f32, x_tensor->data.f32, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_tensor_t* const dcy_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, dcy);
	memcpy(dcy_tensor->data.f32, dy_tensor->data.f32, sizeof(float) * 2 * 2 * 2 * 10);
	ccv_nnc_graph_run(cpu_graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dcx_tensor = ccv_nnc_tensor_from_symbol(cpu_tensor_arena, dcx);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dx_tensor->data.f32, dcx_tensor->data.f32, 2 * 2 * 2 * 10, 2e-3, "batch norm result from cudnn should match the one from reference implementation");
	ccv_nnc_symbolic_graph_free(cpu_symbolic_graph);
	ccv_nnc_tensor_arena_free(cpu_tensor_arena);
	ccv_nnc_graph_exec_arena_free(cpu_graph_exec_arena);
	ccv_nnc_graph_free(cpu_graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dx16_tensor);
}

TEST_CASE("compare average pooling with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_AVERAGE_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t avg_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_AVERAGE_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "avg_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, avg_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_AVERAGE_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y), 0);
	REQUIRE_TENSOR_EQ(y_tensor, cpu_y, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
}

TEST_CASE("compare average pooling with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_AVERAGE_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t avg_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_AVERAGE_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "avg_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, avg_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_AVERAGE_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_tensor_t* const cpu_y16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_y16), TENSOR_LIST(cpu_y), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cpu_y->data.f32, 3 * 3 * 10, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
	ccv_nnc_tensor_free(cpu_y16);
}

TEST_CASE("compare average pooling gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_AVERAGE_POOL_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "dx");
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 3, 3, 10), "dy");
	ccv_nnc_graph_exec_symbol_t avg_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_AVERAGE_POOL_BACKWARD(5, 5), TENSOR_SYMBOL_LIST(dy), TENSOR_SYMBOL_LIST(dx), "avg_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, avg_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	int i;
	for (i = 0; i < 3 * 3 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_from_symbol(tensor_arena, dy);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_AVERAGE_POOL_BACKWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_TENSOR_EQ(dx_tensor, cpu_dx, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(cpu_dx);
}

TEST_CASE("compare average pooling gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_AVERAGE_POOL_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "dx");
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 3, 3, 10), "dy");
	ccv_nnc_graph_exec_symbol_t avg_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_AVERAGE_POOL_BACKWARD(5, 5), TENSOR_SYMBOL_LIST(dy), TENSOR_SYMBOL_LIST(dx), "avg_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, avg_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	int i;
	for (i = 0; i < 3 * 3 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 3, 3, 10), 0);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_from_symbol(tensor_arena, dy);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_AVERAGE_POOL_BACKWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_dx16), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dx_tensor->data.f32, cpu_dx->data.f32, 7 * 7 * 10, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(cpu_dx);
	ccv_nnc_tensor_free(cpu_dx16);
}

TEST_CASE("compare max pooling with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y), 0);
	REQUIRE_TENSOR_EQ(y_tensor, cpu_y, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
}

TEST_CASE("compare max pooling with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 3, 3, 10), 0);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_y16), TENSOR_LIST(cpu_y), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cpu_y->data.f32, 3 * 3 * 10, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
	ccv_nnc_tensor_free(cpu_y16);
}

TEST_CASE("compare max pooling 2x2 with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 10, 6, 6), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 10, 3, 3), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(2, 2), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (0, 0)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 6, 6), 0);
	int i, j;
	for (i = 0; i < 6 * 6 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const gt_x = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 6, 6, 10), 0);
	for (i = 0; i < 10; i++)
		for (j = 0; j < 6 * 6; j++)
			gt_x->data.f32[j * 10 + i] = x_tensor->data.f32[i * 6 * 6 + j];
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const gt_y= ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(2, 2), HINT((2, 2), (0, 0)), 0, TENSOR_LIST(gt_x), TENSOR_LIST(gt_y), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 3, 3), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 3, 3), 0);
	for (i = 0; i < 10; i++)
		for (j = 0; j < 3 * 3; j++)
			y_tensor->data.f32[i * 3 * 3 + j] = gt_y->data.f32[j * 10 + i];
	REQUIRE_TENSOR_EQ(y_tensor, cpu_y, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
}

TEST_CASE("compare max pooling 2x2 with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 10, 6, 6), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 10, 3, 3), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(2, 2), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (0, 0)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 6, 6), 0);
	int i, j;
	for (i = 0; i < 6 * 6 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const gt_x = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 6, 6, 10), 0);
	for (i = 0; i < 10; i++)
		for (j = 0; j < 6 * 6; j++)
			gt_x->data.f32[j * 10 + i] = x_tensor->data.f32[i * 6 * 6 + j];
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 10, 6, 6), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const gt_y= ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(2, 2), HINT((2, 2), (0, 0)), 0, TENSOR_LIST(gt_x), TENSOR_LIST(gt_y), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 10, 3, 3), 0);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 3, 3), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_y16), TENSOR_LIST(cpu_y), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 3, 3), 0);
	for (i = 0; i < 10; i++)
		for (j = 0; j < 3 * 3; j++)
			y_tensor->data.f32[i * 3 * 3 + j] = gt_y->data.f32[j * 10 + i];
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cpu_y->data.f32, 10 * 3 * 3, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
	ccv_nnc_tensor_free(cpu_y16);
}

TEST_CASE("compare max pooling gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	for (i = 0; i < 3 * 3 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_BACKWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(dy_tensor, x_tensor, y_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_TENSOR_EQ(dx_tensor, cpu_dx, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(cpu_dx);
	ccv_nnc_tensor_free(dyt);
}

TEST_CASE("compare max pooling gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_MAX_POOL_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 3, 3, 10), "y");
	ccv_nnc_graph_exec_symbol_t max_pool = ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_MAX_POOL_FORWARD(5, 5), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "max_pool");
	ccv_nnc_graph_exec_symbol_set_hint(symbolic_graph, max_pool, HINT((2, 2), (1, 1)));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	for (i = 0; i < 3 * 3 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 3, 3, 10), 0);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 3, 3, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_FORWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_MAX_POOL_BACKWARD(5, 5), HINT((2, 2), (1, 1)), 0, TENSOR_LIST(dy_tensor, x_tensor, y_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_dx16), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dx_tensor->data.f32, cpu_dx->data.f32, 7 * 7 * 10, 5e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(cpu_dx);
	ccv_nnc_tensor_free(cpu_dx16);
	ccv_nnc_tensor_free(dyt);
}

TEST_CASE("compare relu with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_RELU_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 7, 7, 10), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_RELU_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "relu");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_RELU_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y), 0);
	REQUIRE_TENSOR_EQ(y_tensor, cpu_y, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
}

TEST_CASE("compare relu with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_RELU_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 7, 7, 10), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_RELU_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "relu");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	int i;
	for (i = 0; i < 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_RELU_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const cpu_y16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 7, 7, 10), 0);
	ccv_nnc_tensor_t* const cpu_y = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 7, 7, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(cpu_y16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_y16), TENSOR_LIST(cpu_y), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, y_tensor->data.f32, cpu_y->data.f32, 7 * 7 * 10, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(cpu_y);
	ccv_nnc_tensor_free(cpu_y16);
}

TEST_CASE("compare relu gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_RELU_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_RELU_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 10, 10, 7, 7), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 10, 10, 7, 7), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_RELU_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "relu");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	for (i = 0; i < 10 * 7 * 7 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	for (i = 0; i < 10 * 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_RELU_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_RELU_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor, x_tensor, y_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_TENSOR_EQ(dx_tensor, cpu_dx, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_tensor_free(cpu_dx);
}

TEST_CASE("compare relu gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_RELU_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_RELU_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 10, 10, 7, 7), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 10, 10, 7, 7), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_RELU_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "relu");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	for (i = 0; i < 10 * 7 * 7 * 10; i++)
		dy_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NCHW(000, 16F, 10, 10, 7, 7), 0);
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	for (i = 0; i < 10 * 7 * 7 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt) * 2 - 1;
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_RELU_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_RELU_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor, x_tensor, y_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const cpu_dx16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 10, 10, 7, 7), 0);
	ccv_nnc_tensor_t* const cpu_dx = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 10, 10, 7, 7), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(cpu_dx16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(cpu_dx16), TENSOR_LIST(cpu_dx), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dx_tensor->data.f32, cpu_dx->data.f32, 10 * 10 * 7 * 7, 1e-3, "cudnn result should equal to cpu result");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_tensor_free(cpu_dx);
	ccv_nnc_tensor_free(cpu_dx16);
}

TEST_CASE("compare dropout with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 20 * 50), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 20 * 50), "y");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, ccv_nnc_tensor_auto, "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DROPOUT_FORWARD(0.4), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y, c), "dropout");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	int i;
	for (i = 0; i < 20 * 50; i++)
		x_tensor->data.f32[i] = (i + 1) * 0.01;
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(y_tensor), 0);
	int zero_count = 0;
	for (i = 0; i < 20 * 50; i++)
		if (fabsf(y_tensor->data.f32[i]) < 1e-5)
			++zero_count;
		else {
			REQUIRE_EQ_WITH_TOLERANCE(x_tensor->data.f32[i] / 0.6, y_tensor->data.f32[i], 1e-5, "should be scaled up by 1 / 0.6");
		}
	REQUIRE_EQ_WITH_TOLERANCE((float)zero_count / (20 * 50), 0.4, 5 * 1e-2, "should be within 5%% of error");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
}

TEST_CASE("compare dropout with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 20 * 50), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 20 * 50), "y");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, ccv_nnc_tensor_auto, "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DROPOUT_FORWARD(0.4), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y, c), "dropout");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	int i;
	for (i = 0; i < 20 * 50; i++)
		x_tensor->data.f32[i] = (i + 1) * 0.01;
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 20 * 50), 0);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	ccv_nnc_tensor_t* const y16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(y16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(y16_tensor), TENSOR_LIST(y_tensor), 0);
	int zero_count = 0;
	for (i = 0; i < 20 * 50; i++)
		if (fabsf(y_tensor->data.f32[i]) < 1e-5)
			++zero_count;
		else {
			REQUIRE_EQ_WITH_TOLERANCE(x_tensor->data.f32[i] / 0.6, y_tensor->data.f32[i], x_tensor->data.f32[i] * 2e-3, "should be scaled up by 1 / 0.6");
		}
	REQUIRE_EQ_WITH_TOLERANCE((float)zero_count / (20 * 50), 0.4, 5 * 1e-2, "should be within 5%% of error");
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(y16_tensor);
}

TEST_CASE("compare dropout gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 20 * 50), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 20 * 50), "y");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, ccv_nnc_tensor_auto, "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DROPOUT_FORWARD(0.4), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y, c), "dropout");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	for (i = 0; i < 20 * 50; i++)
		dy_tensor->data.f32[i] = i + 1;
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	for (i = 0; i < 20 * 50; i++)
		x_tensor->data.f32[i] = (i + 1) * 0.01;
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(dx_tensor), 0);
	int zero_count = 0;
	for (i = 0; i < 20 * 50; i++)
		if (fabsf(dx_tensor->data.f32[i]) < 1e-5)
			++zero_count;
		else {
			REQUIRE_EQ_WITH_TOLERANCE(dx_tensor->data.f32[i], dy_tensor->data.f32[i] / 0.6, 1e-3, "should match the gradient");
		}
	REQUIRE_EQ_WITH_TOLERANCE((float)zero_count / (20 * 50), 0.4, 5 * 1e-2, "should be within 5%% of error");
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_tensor_free(dx_tensor);
}

TEST_CASE("compare dropout gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_DROPOUT_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 20 * 50), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 20 * 50), "y");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, ccv_nnc_tensor_auto, "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DROPOUT_FORWARD(0.4), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y, c), "dropout");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	int i;
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	for (i = 0; i < 20 * 50; i++)
		dy_tensor->data.f32[i] = i + 1;
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 20 * 50), 0);
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	for (i = 0; i < 20 * 50; i++)
		x_tensor->data.f32[i] = (i + 1) * 0.01;
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 20 * 50), 0);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 20 * 50), 0);
	ccv_nnc_tensor_t* const dx16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 20 * 50), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(dx16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dx16_tensor), TENSOR_LIST(dx_tensor), 0);
	int zero_count = 0;
	for (i = 0; i < 20 * 50; i++)
		if (fabsf(dx_tensor->data.f32[i]) < 1e-5)
			++zero_count;
		else {
			REQUIRE_EQ_WITH_TOLERANCE(dx_tensor->data.f32[i], dy_tensor->data.f32[i] / 0.6, dx_tensor->data.f32[i] * 1e-3, "should match the gradient");
		}
	REQUIRE_EQ_WITH_TOLERANCE((float)zero_count / (20 * 50), 0.4, 5 * 1e-2, "should be within 5%% of error");
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dx16_tensor);
}

TEST_CASE("compare softmax with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 20, 10), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 32F, 20, 10), "b");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SOFTMAX_FORWARD(), TENSOR_SYMBOL_LIST(a), TENSOR_SYMBOL_LIST(b), "softmax");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 20 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const a_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, a);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(a_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	ccv_nnc_tensor_t* const b_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, b);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(b_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const ty = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(ty), 0);
	REQUIRE_TENSOR_EQ(ty, y_tensor, "softmax from cudnn should match from CPU");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(ty);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare softmax with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 20, 10), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NCHW(000, 16F, 20, 10), "b");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SOFTMAX_FORWARD(), TENSOR_SYMBOL_LIST(a), TENSOR_SYMBOL_LIST(b), "softmax");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, 0, 0, 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 20 * 10; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const a_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, a);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 20, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(a_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const y16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(16F, 20, 10), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	ccv_nnc_tensor_t* const b_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, b);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(b_tensor), TENSOR_LIST(y16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(y16_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const ty = ccv_nnc_tensor_new(0, CPU_TENSOR_NCHW(32F, 20, 10), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(ty), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, ty->data.f32, y_tensor->data.f32, 20 * 10, 1e-3, "softmax from cudnn should match from CPU");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(ty);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare softmax gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 100), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 100), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SOFTMAX_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "softmax");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 100; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	for (i = 0; i < 10 * 100; i++)
		dy_tensor->data.f32[i] = 0;
	for (i = 0; i < 10; i++)
		dy_tensor->data.f32[i * 100 + i] = 1;
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const ty_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(ty_tensor), 0);
	REQUIRE_TENSOR_EQ(ty_tensor, y_tensor, "forward pass should match");
	ccv_nnc_tensor_t* const tdx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor, 0, ty_tensor), TENSOR_LIST(tdx_tensor), 0);
	REQUIRE_TENSOR_EQ(tdx_tensor, dx_tensor, "backward pass should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(ty_tensor);
	ccv_nnc_tensor_free(tdx_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare softmax gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 100), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 100), "y");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_SOFTMAX_FORWARD(), TENSOR_SYMBOL_LIST(x), TENSOR_SYMBOL_LIST(y), "softmax");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(y), TENSOR_SYMBOL_LIST(x), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 100; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dy_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	for (i = 0; i < 10 * 100; i++)
		dy_tensor->data.f32[i] = 0;
	for (i = 0; i < 10; i++)
		dy_tensor->data.f32[i * 100 + i] = 1;
	ccv_nnc_tensor_t* const dy16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* const dyt = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor), TENSOR_LIST(dy16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy16_tensor), TENSOR_LIST(dyt), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(dy, dyt)), 0, 0, SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const xt = ccv_nnc_tensor_from_symbol(tensor_arena, x);
	ccv_nnc_tensor_t* const x16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(x16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x16_tensor), TENSOR_LIST(xt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const dx16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* const dx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* const dxt = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* const y16_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* const yt = ccv_nnc_tensor_from_symbol(tensor_arena, y);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dxt), TENSOR_LIST(dx16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dx16_tensor), TENSOR_LIST(dx_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(yt), TENSOR_LIST(y16_tensor), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(y16_tensor), TENSOR_LIST(y_tensor), 0);
	ccv_nnc_tensor_t* const ty_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor), TENSOR_LIST(ty_tensor), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, ty_tensor->data.f32, y_tensor->data.f32, 10 * 100, 1e-3, "forward pass should match");
	ccv_nnc_tensor_t* const tdx_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dy_tensor, 0, ty_tensor), TENSOR_LIST(tdx_tensor), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, tdx_tensor->data.f32, dx_tensor->data.f32, 10 * 100, 1e-3, "backward pass should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(x16_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(y16_tensor);
	ccv_nnc_tensor_free(dx_tensor);
	ccv_nnc_tensor_free(dx16_tensor);
	ccv_nnc_tensor_free(dy_tensor);
	ccv_nnc_tensor_free(dy16_tensor);
	ccv_nnc_tensor_free(ty_tensor);
	ccv_nnc_tensor_free(tdx_tensor);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare add with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_ADD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), "y");
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 5, 3), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 1, 3), "b");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 5, 3), "c");
	ccv_nnc_tensor_symbol_t z = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "z");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x, y), TENSOR_SYMBOL_LIST(a, b), "transfer");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_ADD_FORWARD(0.5, 0.2), TENSOR_SYMBOL_LIST(a, b), TENSOR_SYMBOL_LIST(c), "add");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(c), TENSOR_SYMBOL_LIST(z), "transfer");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(x, x_tensor), KV(y, y_tensor)), TENSOR_SYMBOL_LIST(z), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10 * 5 * 1 * 3; i++)
		y_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* zt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_FORWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor, y_tensor), TENSOR_LIST(zt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const z_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, z);
	REQUIRE_TENSOR_EQ(zt, z_tensor, "add should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(zt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare add with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_ADD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), "y");
	ccv_nnc_tensor_symbol_t x16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 10, 5, 5, 3), "x 16");
	ccv_nnc_tensor_symbol_t y16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 10, 5, 1, 3), "y 16");
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 5, 3), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 1, 3), "b");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 5, 3), "c");
	ccv_nnc_tensor_symbol_t z = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "z");
	ccv_nnc_tensor_symbol_t z16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 10, 5, 5, 3), "z 16");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATATYPE_CONVERSION_FORWARD(), TENSOR_SYMBOL_LIST(x, y), TENSOR_SYMBOL_LIST(x16, y16), "convert");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x16, y16), TENSOR_SYMBOL_LIST(a, b), "transfer");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_ADD_FORWARD(0.5, 0.2), TENSOR_SYMBOL_LIST(a, b), TENSOR_SYMBOL_LIST(c), "add");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(c), TENSOR_SYMBOL_LIST(z16), "transfer");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATATYPE_CONVERSION_FORWARD(), TENSOR_SYMBOL_LIST(z16), TENSOR_SYMBOL_LIST(z), "convert");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(x, x_tensor), KV(y, y_tensor)), TENSOR_SYMBOL_LIST(z), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10 * 5 * 1 * 3; i++)
		y_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* zt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_FORWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor, y_tensor), TENSOR_LIST(zt), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* const z_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, z);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, zt->data.f32, z_tensor->data.f32, 10 * 5 * 5 * 3, 1e-3, "add should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(zt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare add gradient with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_ADD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_ADD_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), "y");
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 5, 3), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 1, 3), "b");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 32F, 10, 5, 5, 3), "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x, y), TENSOR_SYMBOL_LIST(a, b), "transfer");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_ADD_FORWARD(0.5, 0.2), TENSOR_SYMBOL_LIST(a, b), TENSOR_SYMBOL_LIST(c), "add");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(c), TENSOR_SYMBOL_LIST(x, y), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_tensor_symbol_t dc = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, c);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(x, x_tensor), KV(y, y_tensor)), TENSOR_SYMBOL_LIST(dx, dy), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10 * 5 * 1 * 3; i++)
		y_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* dct = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		dct->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dc_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dc);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dct), TENSOR_LIST(dc_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* zt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_FORWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor, y_tensor), TENSOR_LIST(zt), 0);
	ccv_nnc_tensor_t* dxt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* dyt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_BACKWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(dct, x_tensor, y_tensor, zt), TENSOR_LIST(dxt, dyt), 0);
	ccv_nnc_tensor_t* dx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* dy_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dy);
	REQUIRE_TENSOR_EQ(dxt, dx_tensor, "backward pass should match");
	REQUIRE_TENSOR_EQ(dyt, dy_tensor, "backward pass should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dct);
	ccv_nnc_tensor_free(zt);
	ccv_nnc_tensor_free(dxt);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare add gradient with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_ADD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_ADD_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_symbolic_graph_t* const symbolic_graph = ccv_nnc_symbolic_graph_new();
	ccv_nnc_tensor_symbol_t x = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), "x");
	ccv_nnc_tensor_symbol_t y = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), "y");
	ccv_nnc_tensor_symbol_t x16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 10, 5, 5, 3), "x 16");
	ccv_nnc_tensor_symbol_t y16 = ccv_nnc_tensor_symbol_new(symbolic_graph, CPU_TENSOR_NHWC(16F, 10, 5, 1, 3), "y 16");
	ccv_nnc_tensor_symbol_t a = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 5, 3), "a");
	ccv_nnc_tensor_symbol_t b = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 1, 3), "b");
	ccv_nnc_tensor_symbol_t c = ccv_nnc_tensor_symbol_new(symbolic_graph, GPU_TENSOR_NHWC(000, 16F, 10, 5, 5, 3), "c");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATATYPE_CONVERSION_FORWARD(), TENSOR_SYMBOL_LIST(x, y), TENSOR_SYMBOL_LIST(x16, y16), "convert");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_DATA_TRANSFER_FORWARD(), TENSOR_SYMBOL_LIST(x16, y16), TENSOR_SYMBOL_LIST(a, b), "transfer");
	ccv_nnc_graph_exec_symbol_new(symbolic_graph, CMD_ADD_FORWARD(0.5, 0.2), TENSOR_SYMBOL_LIST(a, b), TENSOR_SYMBOL_LIST(c), "add");
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	ccv_nnc_symbolic_graph_backward(symbolic_graph, TENSOR_SYMBOL_LIST(c), TENSOR_SYMBOL_LIST(x, y), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph));
	ccv_nnc_graph_exec_symbol_autogen(symbolic_graph, 0, 0, CCV_NNC_AUTOGEN_ALL_EXECS | CCV_NNC_AUTOGEN_SOURCES_AND_DESTINATIONS);
	SYMBOLIC_GRAPH_GEN(symbolic_graph, CCV_NNC_LONG_DOT_GRAPH);
	ccv_nnc_tensor_t* const x_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* const y_tensor = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_graph_t* graph = 0;
	ccv_nnc_tensor_arena_t* tensor_arena = 0;
	ccv_nnc_graph_exec_arena_t* graph_exec_arena = 0;
	ccv_nnc_tensor_symbol_t dc = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, c);
	ccv_nnc_tensor_symbol_t dx = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, x);
	ccv_nnc_tensor_symbol_t dy = ccv_nnc_tensor_symbol_for_backward(symbolic_graph, y);
	ccv_nnc_symbolic_graph_compile(symbolic_graph, TENSOR_BIND_MAP(KV(x, x_tensor), KV(y, y_tensor)), TENSOR_SYMBOL_LIST(dx, dy), SYMBOLIC_GRAPH_SOURCES(symbolic_graph), SYMBOLIC_GRAPH_DESTINATIONS(symbolic_graph), &graph, &tensor_arena, &graph_exec_arena);
	GRAPH_GEN(graph, CCV_NNC_LONG_DOT_GRAPH);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		x_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10 * 5 * 1 * 3; i++)
		y_tensor->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* dct = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* dct16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 5, 5, 3), 0);
	for (i = 0; i < 10 * 5 * 5 * 3; i++)
		dct->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_tensor_t* const dc_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dc);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dct), TENSOR_LIST(dct16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(dct16), TENSOR_LIST(dc_tensor), 0);
	ccv_nnc_graph_run(graph, 0, 0, 0, TRAVERSE_FULL);
	ccv_nnc_tensor_t* zt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_FORWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(x_tensor, y_tensor), TENSOR_LIST(zt), 0);
	ccv_nnc_tensor_t* dxt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 5, 3), 0);
	ccv_nnc_tensor_t* dyt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 5, 1, 3), 0);
	ccv_nnc_cmd_exec(CMD_ADD_BACKWARD(0.5, 0.2), ccv_nnc_no_hint, 0, TENSOR_LIST(dct, x_tensor, y_tensor, zt), TENSOR_LIST(dxt, dyt), 0);
	ccv_nnc_tensor_t* dx_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dx);
	ccv_nnc_tensor_t* dy_tensor = ccv_nnc_tensor_from_symbol(tensor_arena, dy);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dxt->data.f32, dx_tensor->data.f32, 10 * 5 * 5 * 3, 1e-3, "backward pass should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, dyt->data.f32, dy_tensor->data.f32, 10 * 5 * 1 * 3, 1e-3, "backward pass should match");
	ccv_nnc_tensor_free(x_tensor);
	ccv_nnc_tensor_free(y_tensor);
	ccv_nnc_tensor_free(dct);
	ccv_nnc_tensor_free(dct16);
	ccv_nnc_tensor_free(zt);
	ccv_nnc_tensor_free(dxt);
	ccv_nnc_tensor_free(dyt);
	ccv_nnc_graph_free(graph);
	ccv_nnc_tensor_arena_free(tensor_arena);
	ccv_nnc_graph_exec_arena_free(graph_exec_arena);
	ccv_nnc_symbolic_graph_free(symbolic_graph);
}

TEST_CASE("compare SGD with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gm), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
}

TEST_CASE("compare SGD with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const g16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const a16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const m16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const b16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const n16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(g16, a16, m16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g16, a16, m16), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gbt16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const gnt16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt16, gnt16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gbt16, gnt16), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a16, m16), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt16, gnt16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gbt16, gnt16), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(g16);
	ccv_nnc_tensor_free(a16);
	ccv_nnc_tensor_free(m16);
	ccv_nnc_tensor_free(b16);
	ccv_nnc_tensor_free(n16);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
	ccv_nnc_tensor_free(gbt16);
	ccv_nnc_tensor_free(gnt16);
}

TEST_CASE("compare SGD with cudnn in mixed precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const g16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g), TENSOR_LIST(g16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g16, a, m), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(0, 0.9, 0.5, 0.999, 0.9, 0.9), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(g16);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
}

TEST_CASE("compare Nesterov SGD with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gm), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_TENSOR_EQ(gbt, b, "cpu result should match");
	REQUIRE_TENSOR_EQ(gnt, n, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
}

TEST_CASE("compare Nesterov SGD with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const g16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const a16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const m16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const b16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const n16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(g16, a16, m16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g16, a16, m16), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gbt16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* const gnt16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt16, gnt16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gbt16, gnt16), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a16, m16), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt16, gnt16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gbt16, gnt16), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(g16);
	ccv_nnc_tensor_free(a16);
	ccv_nnc_tensor_free(m16);
	ccv_nnc_tensor_free(b16);
	ccv_nnc_tensor_free(n16);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
	ccv_nnc_tensor_free(gbt16);
	ccv_nnc_tensor_free(gnt16);
}

TEST_CASE("compare Nesterov SGD with cudnn in mixed precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SGD_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const g = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const m = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const n = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const g16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i;
	for (i = 0; i < 10; i++)
		g->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		a->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		m->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(g, a, m), TENSOR_LIST(b, n), 0);
	ccv_nnc_tensor_t* const gg = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* const ga = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gm = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g), TENSOR_LIST(g16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g16, a, m), TENSOR_LIST(gg, ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_tensor_t* const gbt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* const gnt = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ga, gm), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_t* const gb = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* const gn = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, m), TENSOR_LIST(ga, gm), 0);
	ccv_nnc_cmd_exec(CMD_SGD_FORWARD(1, 0.9, 0.5, 0.999, 0.9, 0), ccv_nnc_no_hint, 0, TENSOR_LIST(gg, ga, gm), TENSOR_LIST(gb, gn), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(gb, gn), TENSOR_LIST(gbt, gnt), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gbt->data.f32, b->data.f32, 10, 1e-3, "cpu result should match");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, gnt->data.f32, n->data.f32, 10, 1e-3, "cpu result should match");
	ccv_nnc_tensor_free(g);
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(m);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(n);
	ccv_nnc_tensor_free(g16);
	ccv_nnc_tensor_free(gg);
	ccv_nnc_tensor_free(ga);
	ccv_nnc_tensor_free(gm);
	ccv_nnc_tensor_free(gb);
	ccv_nnc_tensor_free(gn);
	ccv_nnc_tensor_free(gbt);
	ccv_nnc_tensor_free(gnt);
}

TEST_CASE("compare softmax cross entropy forward")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_tensor_t* ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i = 0;
	for (i = 0; i < 1000; i++)
		ha->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		hb->data.f32[i] = (i + 1) * 9;
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(a, b), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(hc, hd), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b), TENSOR_LIST(c, d), 0);
	ccv_nnc_tensor_t* tc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* td = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(c, d), TENSOR_LIST(tc, td), 0);
	REQUIRE_TENSOR_EQ(tc, hc, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_TENSOR_EQ(td, hd, "GPU computed output should be the same as CPU computed ones");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(tc);
	ccv_nnc_tensor_free(td);
}

TEST_CASE("compare softmax cross entropy forward in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_tensor_t* ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* ha16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* hb16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i = 0;
	for (i = 0; i < 1000; i++)
		ha->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		hb->data.f32[i] = (i + 1) * 9;
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(ha16, hb16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha16, hb16), TENSOR_LIST(a, b), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(hc, hd), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b), TENSOR_LIST(c, d), 0);
	ccv_nnc_tensor_t* tc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* td = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* tc16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* td16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(c, d), TENSOR_LIST(tc16, td16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(tc16, td16), TENSOR_LIST(tc, td), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, tc->data.f32, hc->data.f32, 10, 1e-3, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, td->data.f32, hd->data.f32, 10 * 100, 1e-3, "GPU computed output should be the same as CPU computed ones");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(ha16);
	ccv_nnc_tensor_free(hb16);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(tc);
	ccv_nnc_tensor_free(td);
	ccv_nnc_tensor_free(tc16);
	ccv_nnc_tensor_free(td16);
}

TEST_CASE("compare softmax cross entropy backward")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_tensor_t* g = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10), 0);
	ccv_nnc_tensor_t* h = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 10, 100), 0);
	ccv_nnc_tensor_t* ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hg = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hh = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i = 0;
	for (i = 0; i < 1000; i++)
		ha->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		hb->data.f32[i] = (i + 1) * 9;
	for (i = 0; i < 10; i++)
		hg->data.f32[i] = i * 0.1;
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb, hg), TENSOR_LIST(a, b, g), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(hc, hd), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(hg, 0, 0, hb, hc, hd), TENSOR_LIST(hh, 0), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b), TENSOR_LIST(c, d), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, 0, 0, b, c, d), TENSOR_LIST(h, 0), 0);
	ccv_nnc_tensor_t* tc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* td = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* th = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(c, d, h), TENSOR_LIST(tc, td, th), 0);
	REQUIRE_TENSOR_EQ(tc, hc, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_TENSOR_EQ(td, hd, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_TENSOR_EQ(th, hh, "GPU computed output should be the same as CPU computed ones");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(h);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(hg);
	ccv_nnc_tensor_free(hh);
	ccv_nnc_tensor_free(tc);
	ccv_nnc_tensor_free(td);
	ccv_nnc_tensor_free(th);
}

TEST_CASE("compare softmax cross entropy backward in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN) &&
		ccv_nnc_cmd_ok(CCV_NNC_SOFTMAX_CROSSENTROPY_BACKWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_tensor_t* b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_tensor_t* g = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10), 0);
	ccv_nnc_tensor_t* h = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 10, 100), 0);
	ccv_nnc_tensor_t* ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* ha16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* hb16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* hg = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* hg16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* hh = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	dsfmt_t dsfmt;
	dsfmt_init_gen_rand(&dsfmt, 0);
	int i = 0;
	for (i = 0; i < 1000; i++)
		ha->data.f32[i] = dsfmt_genrand_open_close(&dsfmt);
	for (i = 0; i < 10; i++)
		hb->data.f32[i] = (i + 1) * 9;
	for (i = 0; i < 10; i++)
		hg->data.f32[i] = i * 0.1;
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb, hg), TENSOR_LIST(ha16, hb16, hg16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha16, hb16, hg16), TENSOR_LIST(a, b, g), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb), TENSOR_LIST(hc, hd), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(hg, 0, 0, hb, hc, hd), TENSOR_LIST(hh, 0), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b), TENSOR_LIST(c, d), 0);
	ccv_nnc_cmd_exec(CMD_SOFTMAX_CROSSENTROPY_BACKWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(g, 0, 0, b, c, d), TENSOR_LIST(h, 0), 0);
	ccv_nnc_tensor_t* tc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10), 0);
	ccv_nnc_tensor_t* td = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* th = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 10, 100), 0);
	ccv_nnc_tensor_t* tc16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10), 0);
	ccv_nnc_tensor_t* td16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_tensor_t* th16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 10, 100), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(c, d, h), TENSOR_LIST(tc16, td16, th16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(tc16, td16, th16), TENSOR_LIST(tc, td, th), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, tc->data.f32, hc->data.f32, 10, 1e-3, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, td->data.f32, hd->data.f32, 10 * 100, 1e-3, "GPU computed output should be the same as CPU computed ones");
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, th->data.f32, hh->data.f32, 10 * 100, 1e-3, "GPU computed output should be the same as CPU computed ones");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(h);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(ha16);
	ccv_nnc_tensor_free(hb16);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(hg);
	ccv_nnc_tensor_free(hg16);
	ccv_nnc_tensor_free(hh);
	ccv_nnc_tensor_free(tc);
	ccv_nnc_tensor_free(td);
	ccv_nnc_tensor_free(th);
	ccv_nnc_tensor_free(tc16);
	ccv_nnc_tensor_free(td16);
	ccv_nnc_tensor_free(th16);
}

TEST_CASE("compare ewsum with cudnn")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_EWSUM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 100), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 100), 0);
	ccv_nnc_tensor_t* const c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 100), 0);
	ccv_nnc_tensor_t* const d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 32F, 100), 0);
	ccv_nnc_tensor_t* const ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const gd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	int i;
	for (i = 0; i < 100; i++)
	{
		ha->data.f32[i] = 1;
		hb->data.f32[i] = 0.5;
		hc->data.f32[i] = 0.25;
		gd->data.f32[i] = 1.75;
	}
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb, hc), TENSOR_LIST(a, b, c), 0);
	ccv_nnc_cmd_exec(CMD_EWSUM_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b, c), TENSOR_LIST(d), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(d), TENSOR_LIST(hd), 0);
	REQUIRE_TENSOR_EQ(hd, gd, "ewsum result should be the same");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(gd);
}

TEST_CASE("compare ewsum with cudnn in half precision")
{
	GUARD_ELSE_RETURN(ccv_nnc_cmd_ok(CCV_NNC_EWSUM_FORWARD, CCV_NNC_BACKEND_GPU_CUDNN));
	ccv_nnc_tensor_t* const a = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 100), 0);
	ccv_nnc_tensor_t* const b = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 100), 0);
	ccv_nnc_tensor_t* const c = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 100), 0);
	ccv_nnc_tensor_t* const d = ccv_nnc_tensor_new(0, GPU_TENSOR_NHWC(000, 16F, 100), 0);
	ccv_nnc_tensor_t* const ha = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hb = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hc = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const hd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	ccv_nnc_tensor_t* const ha16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 100), 0);
	ccv_nnc_tensor_t* const hb16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 100), 0);
	ccv_nnc_tensor_t* const hc16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 100), 0);
	ccv_nnc_tensor_t* const hd16 = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(16F, 100), 0);
	ccv_nnc_tensor_t* const gd = ccv_nnc_tensor_new(0, CPU_TENSOR_NHWC(32F, 100), 0);
	int i;
	for (i = 0; i < 100; i++)
	{
		ha->data.f32[i] = 1;
		hb->data.f32[i] = 0.5;
		hc->data.f32[i] = 0.25;
		gd->data.f32[i] = 1.75;
	}
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha, hb, hc), TENSOR_LIST(ha16, hb16, hc16), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(ha16, hb16, hc16), TENSOR_LIST(a, b, c), 0);
	ccv_nnc_cmd_exec(CMD_EWSUM_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(a, b, c), TENSOR_LIST(d), 0);
	ccv_nnc_cmd_exec(CMD_DATA_TRANSFER_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(d), TENSOR_LIST(hd16), 0);
	ccv_nnc_cmd_exec(CMD_DATATYPE_CONVERSION_FORWARD(), ccv_nnc_no_hint, 0, TENSOR_LIST(hd16), TENSOR_LIST(hd), 0);
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(float, hd->data.f32, gd->data.f32, 100, 1e-3, "ewsum result should be the same");
	ccv_nnc_tensor_free(a);
	ccv_nnc_tensor_free(b);
	ccv_nnc_tensor_free(c);
	ccv_nnc_tensor_free(d);
	ccv_nnc_tensor_free(ha);
	ccv_nnc_tensor_free(hb);
	ccv_nnc_tensor_free(hc);
	ccv_nnc_tensor_free(hd);
	ccv_nnc_tensor_free(ha16);
	ccv_nnc_tensor_free(hb16);
	ccv_nnc_tensor_free(hc16);
	ccv_nnc_tensor_free(hd16);
	ccv_nnc_tensor_free(gd);
}

#include "case_main.h"
