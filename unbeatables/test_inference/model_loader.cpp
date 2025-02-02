#include "model_loader.hpp"
#include "tensorflow/core/public/session.h"
#include "tensorflow/cc/ops/const_op.h"
#include "tensorflow/cc/ops/image_ops.h"
#include "tensorflow/cc/ops/standard_ops.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/graph/default_device.h"
#include "tensorflow/core/graph/graph_def_builder.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/lib/core/stringpiece.h"
#include "tensorflow/core/lib/core/threadpool.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/strings/stringprintf.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/types.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/core/util/command_line_flags.h"
#include "tensorflow/core/framework/tensor_slice.h"

Status ReadImageFile(const string &filename, vector<Tensor>* out_tensors)
{

	//@TODO: Check if filename is valid

	using namespace ::tensorflow::ops;
	Scope root = Scope::NewRootScope();
	auto output = tensorflow::ops::ReadFile(root.WithOpName("file_reader"), filename);


	tensorflow::Output image_reader;
	const int wanted_channels = 3;
	image_reader = tensorflow::ops::DecodeJpeg(root.WithOpName("file_decoder"), output, DecodeJpeg::Channels(wanted_channels));
	std::cout << "second block" << std::endl;


	auto image_unit8 = Cast(root.WithOpName("uint8_caster"), image_reader, tensorflow::DT_UINT8);
	auto image_expanded = ExpandDims(root.WithOpName("expand_dims"), image_unit8, 0);
	std::cout << "third block" << std::endl;

	tensorflow::GraphDef graph;
	auto s = (root.ToGraphDef(&graph));
	std::cout << "fourth block" << std::endl;


	if (!s.ok()){
		printf("Error in loading image from file\n");
	}

	ClientSession session(root);

	auto run_status = session.Run({image_expanded}, out_tensors);
	if (!run_status.ok()){
		printf("Error in running session \n");
	}
	return Status::OK();

}

ModelLoader::ModelLoader(string path)
{
	session_options.config.mutable_gpu_options()->set_allow_growth(true);
	auto status = tensorflow::LoadSavedModel(session_options, run_options, path, {"serve"},
			&bundle);

	if (status.ok()) {
		printf("Model loaded successfully...\n");
	}
	else {
		printf("Error in loading model\n");
	}

}

void ModelLoader::predict(string filename, Prediction &out_pred)
{
	vector<Tensor> image_output;
	auto read_status = ReadImageFile(filename, &image_output);
	make_prediction(image_output, out_pred);
}

void ModelLoader::make_prediction(vector<Tensor> &image_output, Prediction &out_pred)
{
	const string input_node = "serving_default_input_tensor:0";
	vector<pair<string, Tensor>> inputs_data  = {{input_node, image_output[0]}};
	vector<string> output_nodes = {{"StatefulPartitionedCall:0", //detection_anchor_indices
				"StatefulPartitionedCall:1", //detection_boxes
				"StatefulPartitionedCall:2", //detection_classes
				"StatefulPartitionedCall:3",//detection_multiclass_scores
				"StatefulPartitionedCall:4", //detection_scores
				"StatefulPartitionedCall:5"}}; //nudetections


	vector<Tensor> predictions;
	this->bundle.GetSession()->Run(inputs_data, output_nodes, {}, &predictions);

	auto predicted_boxes = predictions[1].tensor<float, 3>();
	auto predicted_scores = predictions[4].tensor<float, 2>();
	auto predicted_labels = predictions[2].tensor<float, 2>();

	//inflate with predictions
	(*out_pred.boxes).clear();
	(*out_pred.scores).clear();
	(*out_pred.labels).clear();
	for (int i=0; i < 20; i++){
		vector<float> coords;
		for (int j=0; j <4 ; j++){
			coords.push_back( predicted_boxes(0, i, j));
		}
		(*out_pred.boxes).push_back(coords);
		(*out_pred.scores).push_back(predicted_scores(0, i));
		(*out_pred.labels).push_back(predicted_labels(0, i));
	}
}
