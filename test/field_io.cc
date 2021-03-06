#include <apf.h>
#include <apfMDS.h>
#include <apfMesh2.h>
#include <apfShape.h>
#include <gmi_mesh.h>
#include <PCU.h>
#include <pcu_util.h>

int main(int argc, char** argv)
{
  PCU_ALWAYS_ASSERT(argc == 3);
  MPI_Init(&argc,&argv);
  PCU_Comm_Init();
  gmi_register_mesh();
  {
  apf::Mesh2* m = apf::loadMdsMesh(argv[1], argv[2]);
  apf::Field* f = apf::createLagrangeField(m, "foo", apf::VECTOR, 1);
  apf::MeshIterator* it = m->begin(0);
  apf::MeshEntity* vert;
  while ((vert = m->iterate(it))) {
    apf::setVector(f, vert, 0, apf::Vector3(1,2,3));
  }
  m->end(it);
  m->writeNative("tmp.smb");
  m->destroyNative();
  apf::destroyMesh(m);
  }
  {
  apf::Mesh2* m = apf::loadMdsMesh(argv[1], "tmp.smb");
  apf::Field* f = m->findField("foo");
  PCU_ALWAYS_ASSERT(f);
  PCU_ALWAYS_ASSERT(apf::VECTOR == apf::getValueType(f));
  PCU_ALWAYS_ASSERT(apf::getLagrange(1) == apf::getShape(f));
  apf::MeshIterator* it = m->begin(0);
  apf::MeshEntity* vert;
  while ((vert = m->iterate(it))) {
    apf::Vector3 vec;
    apf::getVector(f, vert, 0, vec);
    PCU_ALWAYS_ASSERT(vec[0] == 1);
    PCU_ALWAYS_ASSERT(vec[1] == 2);
    PCU_ALWAYS_ASSERT(vec[2] == 3);
  }
  m->end(it);
  m->destroyNative();
  apf::destroyMesh(m);
  }
  PCU_Comm_Free();
  MPI_Finalize();
}
