#include "ma.h"
#include <apf.h>
#include <gmi_mesh.h>
#include <gmi_sim.h>
#include <apfMDS.h>
#include <apfShape.h>
#include <PCU.h>
#include <SimUtil.h>
#include <SimModel.h>

class Linear : public ma::IsotropicFunction
{
  public:
    Linear(ma::Mesh* m)
    {
      mesh = m;
      average = ma::getAverageEdgeLength(m);
      ma::getBoundingBox(m,lower,upper);
    }
    virtual double getValue(ma::Entity* v)
    {
      ma::Vector p = ma::getPosition(mesh,v);
      double x = (p[0] - lower[0])/(upper[0] - lower[0]);
      return average*(3*x+1)/2;
    }
  private:
    ma::Mesh* mesh;
    double average;
    ma::Vector lower;
    ma::Vector upper;
};

int main(int argc, char** argv)
{
  assert(argc==3);
  const char* modelFile = argv[1];
  const char* meshFile = argv[2];
  MPI_Init(&argc,&argv);
  PCU_Comm_Init();
  Sim_readLicenseFile(0);
  SimModel_start();
  gmi_register_mesh();
  gmi_register_sim();
  ma::Mesh* m = apf::loadMdsMesh(modelFile,meshFile);
  m->verify();
  Linear sf(m);
  ma::adapt(m, &sf);
  m->verify();
  apf::writeVtkFiles("after",m);
  m->destroyNative();
  apf::destroyMesh(m);
  PCU_Comm_Free();
  SimModel_stop();
  Sim_unregisterAllKeys();
  MPI_Finalize();
}

