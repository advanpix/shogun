/*
 * This software is distributed under BSD 3-clause license (see LICENSE file).
 *
 * Authors: Soeren Sonnenburg, Heiko Strathmann, Leon Kuchenbecker, 
 *          Sergey Lisitsyn
 */

#include <shogun/classifier/svm/LibSVM.h>
#include <shogun/io/SGIO.h>
#include <shogun/labels/BinaryLabels.h>

using namespace shogun;

CLibSVM::CLibSVM()
: CSVM(), solver_type(LIBSVM_C_SVC)
{
	register_params();
}

CLibSVM::CLibSVM(LIBSVM_SOLVER_TYPE st)
: CSVM(), solver_type(st)
{
	register_params();
}


CLibSVM::CLibSVM(float64_t C, CKernel* k, CLabels* lab, LIBSVM_SOLVER_TYPE st)
: CSVM(C, k, lab), solver_type(st)
{
	register_params();
}

CLibSVM::~CLibSVM()
{
}

void CLibSVM::register_params()
{
	SG_ADD((machine_int_t*) &solver_type, "libsvm_solver_type", "LibSVM Solver type", MS_NOT_AVAILABLE);
}

bool CLibSVM::train_machine(CFeatures* data)
{
	svm_problem problem;
	svm_parameter param;
	struct svm_model* model = nullptr;

	struct svm_node* x_space;

	ASSERT(m_labels && m_labels->get_num_labels())

	if (data)
	{
		if (m_labels->get_num_labels() != data->get_num_vectors())
		{
			SG_ERROR("%s::train_machine(): Number of training vectors (%d) does"
					" not match number of labels (%d)\n", get_name(),
					data->get_num_vectors(), m_labels->get_num_labels());
		}
		kernel->init(data, data);
	}
	REQUIRE(
	    kernel->get_num_vec_lhs() == m_labels->get_num_labels(),
	    "Number of training data (%d) must match number of labels (%d)\n",
	    kernel->get_num_vec_lhs(), m_labels->get_num_labels())

	problem.l=m_labels->get_num_labels();
	SG_INFO("%d trainlabels\n", problem.l)

	// set linear term
	if (m_linear_term.vlen>0)
	{
		if (m_labels->get_num_labels()!=m_linear_term.vlen)
			SG_ERROR("Number of training vectors does not match length of linear term\n")

		// set with linear term from base class
		problem.pv = get_linear_term_array();
	}
	else
	{
		// fill with minus ones
		problem.pv = SG_MALLOC(float64_t, problem.l);

		for (int i=0; i!=problem.l; i++)
			problem.pv[i] = -1.0;
	}

	problem.y=SG_MALLOC(float64_t, problem.l);
	problem.x=SG_MALLOC(struct svm_node*, problem.l);
	problem.C=SG_MALLOC(float64_t, problem.l);

	x_space=SG_MALLOC(struct svm_node, 2*problem.l);

	auto labels = binary_labels(m_labels);
	for (int32_t i=0; i<problem.l; i++)
	{
		problem.y[i] = labels->get_label(i);
		problem.x[i]=&x_space[2*i];
		x_space[2*i].index=i;
		x_space[2*i+1].index=-1;
	}

	int32_t weights_label[2]={-1,+1};
	float64_t weights[2]={1.0,get_C2()/get_C1()};

	ASSERT(kernel && kernel->has_features())

	switch (solver_type)
	{
	case LIBSVM_C_SVC:
		param.svm_type=C_SVC;
		break;
	case LIBSVM_NU_SVC:
		param.svm_type=NU_SVC;
		break;
	default:
		SG_ERROR("%s::train_machine(): Unknown solver type!\n", get_name());
		break;
	}

	param.kernel_type = LINEAR;
	param.degree = 3;
	param.gamma = 0;	// 1/k
	param.coef0 = 0;
	param.nu = get_nu();
	param.kernel=kernel;
	param.cache_size = kernel->get_cache_size();
	param.max_train_time = m_max_train_time;
	param.C = get_C1();
	param.eps = epsilon;
	param.p = 0.1;
	param.shrinking = 1;
	param.nr_weight = 2;
	param.weight_label = weights_label;
	param.weight = weights;
	param.use_bias = get_bias_enabled();

	const char* error_msg = svm_check_parameter(&problem, &param);

	if(error_msg)
		SG_ERROR("Error: %s\n",error_msg)

	model = svm_train(&problem, &param);

	if (model)
	{
		ASSERT(model->nr_class==2)
		ASSERT((model->l==0) || (model->l>0 && model->SV && model->sv_coef && model->sv_coef[0]))

		int32_t num_sv=model->l;

		create_new_model(num_sv);
		CSVM::set_objective(model->objective);

		float64_t sgn=model->label[0];

		set_bias(-sgn*model->rho[0]);

		for (int32_t i=0; i<num_sv; i++)
		{
			set_support_vector(i, (model->SV[i])->index);
			set_alpha(i, sgn*model->sv_coef[0][i]);
		}

		SG_FREE(problem.x);
		SG_FREE(problem.y);
		SG_FREE(problem.pv);
		SG_FREE(problem.C);


		SG_FREE(x_space);

		svm_destroy_model(model);
		model=NULL;
		return true;
	}
	else
		return false;
}
