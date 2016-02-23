#include "apf.h"
#include "apfMDS.h"
#include "apfMesh2.h"
#include "pcu_io.h"
#include "pcu_byteorder.h"

#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdlib>

/*
read files in the AFLR3 format from Dave Marcum at Mississippi State
-little-endian if file has suffix '.lb8.ugrid'
-big-endian if file has suffix '.b8.ugrid'
-integers are 4B
-floats are 8B
*/

namespace {
  int faceTypeIdx(int type) {
    switch(type) {
      case apf::Mesh::TRIANGLE: return 0;
      case apf::Mesh::QUAD: return 1;
      default: {
                 assert(false);
                 return -1;
               }
    }
  }

  struct Reader {
    apf::Mesh2* mesh;
    FILE* file;
    std::map<long, apf::MeshEntity*> nodeMap;
    unsigned* faceVerts[2];
    unsigned* faceTags[2];
    bool swapBytes;
  };

  struct header {
    unsigned nvtx, ntri, nquad, ntet, npyr, nprz, nhex;
    void print() {
      fprintf(stderr,
          "nvtx %u ntri %u nquad %u ntet %u npyr %u nprz %u nhex %u\n",
          nvtx,ntri,nquad,ntet,npyr,nprz,nhex);
    }
  };

  void initReader(Reader* r, apf::Mesh2* m, const char* filename) {
    r->mesh = m;
    r->file = fopen(filename, "rb");
    if (!r->file) {
      fprintf(stderr,"ERROR couldn't open ugrid file \"%s\"\n",filename);
      abort();
    }
    unsigned endian = -1;
    if ( strstr(filename, ".b8.ugrid") ) {
      endian = PCU_BIG_ENDIAN;
    } else if ( strstr(filename, ".lb8.ugrid") ) {
      endian = PCU_LITTLE_ENDIAN;
    } else {
      fprintf(stderr,
          "ERROR file extension of \"%s\" is not supported\n", filename);
      exit(EXIT_FAILURE);
    }
    r->swapBytes = ( endian != PCU_HOST_ORDER );
  }

  void readUnsigneds(FILE* f, unsigned* v, size_t cnt, bool swap) {
    assert(sizeof(unsigned)==4);
    size_t read = fread(v, sizeof(unsigned), cnt, f);
    assert(read == cnt);
    if ( swap )
      pcu_swap_unsigneds(v,cnt);
  }

  void readDoubles(FILE* f, double* vals, size_t cnt, bool swap) {
    assert(sizeof(double)==8);
    size_t read = fread(vals, sizeof(double), cnt, f);
    assert(read == cnt);
    if ( swap )
      pcu_swap_doubles(vals,cnt);
  }

  void readHeader(Reader* r, header* h) {
    const unsigned biggest = 100*1000*1000;
    unsigned headerVals[7];
    readUnsigneds(r->file, headerVals, 7, r->swapBytes);
    for(unsigned i=0; i<7; i++) {
      assert(headerVals[i] < biggest);
    }
    h->nvtx = headerVals[0];
    h->ntri = headerVals[1];
    h->nquad = headerVals[2];
    h->ntet = headerVals[3];
    h->npyr = headerVals[4];
    h->nprz = headerVals[5];
    h->nhex = headerVals[6];
  }

  apf::MeshEntity* makeVtx(Reader* r,
      apf::Vector3& pt, apf::ModelEntity* g) {
    apf::MeshEntity* v = r->mesh->createVert(g);
    r->mesh->setPoint(v, 0, pt);
    return v;
  }

  int ftnToC(long id) {
    return --id;
  }

  apf::MeshEntity* lookupVert(Reader* r, long ftnNodeId) {
    const long cNodeId = ftnToC(ftnNodeId);
    assert(r->nodeMap.count(cNodeId));
    return r->nodeMap[cNodeId];
  }

  void readNodes(Reader* r, header* h) {
    const unsigned dim = 3;
    size_t cnt = h->nvtx*dim;
    double* xyz = (double*) calloc(cnt,sizeof(double));
    readDoubles(r->file, xyz, cnt, r->swapBytes);
    for(long id=0; id<h->nvtx; id++) {
      apf::Vector3 p;
      for(unsigned j=0; j<dim; j++)
        p[j] = xyz[id*dim+j];
      r->nodeMap[id] = makeVtx(r,p,0);
    }
    free(xyz);
    fprintf(stderr, "read %d vtx\n", h->nvtx);
  }

  void setNodeIds(Reader* r, header* h) {
    apf::Mesh* m = r->mesh;
    apf::MeshTag* t = m->createIntTag("ugrid-vtx-ids",1);
    for(long lid=0; lid<h->nvtx; lid++) {
      int iid = lid;
      m->setIntTag(r->nodeMap[lid],t,&iid);
    }
  }

  void readFaces(Reader* r, unsigned nfaces, int apfType) {
    const unsigned nverts = apf::Mesh::adjacentCount[apfType][0];
    size_t cnt = nfaces * nverts;
    unsigned* vtx = (unsigned*) calloc(cnt,sizeof(unsigned));
    r->faceVerts[faceTypeIdx(apfType)] = vtx;
    readUnsigneds(r->file, vtx, cnt, r->swapBytes);
  }

  void readTags(Reader* r, unsigned nfaces, int apfType) {
    unsigned* tags = (unsigned*) calloc(nfaces,sizeof(unsigned));
    r->faceTags[faceTypeIdx(apfType)] = tags;
    readUnsigneds(r->file, tags, nfaces, r->swapBytes);
  }

  void readFacesAndTags(Reader* r, header* h) {
    //read the face vertices and store them
    readFaces(r,h->ntri,apf::Mesh::TRIANGLE);
    readFaces(r,h->nquad,apf::Mesh::QUAD);
    //read the tags and store them
    readTags(r,h->ntri,apf::Mesh::TRIANGLE);
    readTags(r,h->nquad,apf::Mesh::QUAD);
  }

  void checkFilePos(Reader* r, header* h) {
    // seven headers, vtx coords, face vertex ids, and face tags
    long expected = h->nvtx*3*sizeof(double) +
      sizeof(unsigned)*(7 + h->ntri*3 + h->nquad*4 + (h->ntri+h->nquad));
    long pos = ftell(r->file);
    assert( pos == expected );
  }

  void setFaceTags(Reader* r, apf::MeshTag* t, unsigned nfaces, int apfType) {
    const unsigned nverts = apf::Mesh::adjacentCount[apfType][0];
    unsigned* vtx = r->faceVerts[faceTypeIdx(apfType)];
    unsigned* tags = r->faceTags[faceTypeIdx(apfType)];
    for(unsigned id=0; id<nfaces; id++) {
      apf::Downward verts;
      for(unsigned j=0; j<nverts; j++) {
        verts[j] = lookupVert(r, vtx[id*nverts+j]);
      }
      apf::MeshEntity* f =
        apf::findElement(r->mesh, apfType, verts);
      assert(f);
      int val = tags[id];
      r->mesh->setIntTag(f, t, &val);
    }
    free(vtx);
    free(tags);
    fprintf(stderr, "set %d %s face tags\n",
        nfaces, apf::Mesh::typeName[apfType]);
  }

  void setFaceTags(Reader* r, header* h) {
    apf::MeshTag* t = r->mesh->createIntTag("ugrid-face-tag", 1);
    setFaceTags(r,t,h->ntri,apf::Mesh::TRIANGLE);
    setFaceTags(r,t,h->nquad,apf::Mesh::QUAD);
  }

  inline unsigned ugridToMdsElmIdx(int apfType, int ugridIdx) {
    static int ugrid_to_mds_verts[4][8] = {
      {0, 1, 2, 3, -1, -1, -1, -1}, //tet
      {0, 1, 2, 3,  4,  5,  6,  7}, //hex
      {0, 1, 2, 3,  4,  5, -1, -1}, //prism
      {3, 2, 4, 0,  1, -1, -1, -1}  //pyramid
    };
    assert(apfType >= 4);
    return ugrid_to_mds_verts[apfType-4][ugridIdx];
  }

  void readElms(Reader* r, unsigned nelms, int apfType) {
    const unsigned nverts = apf::Mesh::adjacentCount[apfType][0];
    apf::ModelEntity* g = r->mesh->findModelEntity(3, 0);
    size_t cnt = nelms*nverts;
    unsigned* vtx = (unsigned*) calloc(cnt,sizeof(unsigned));
    readUnsigneds(r->file, vtx, cnt, r->swapBytes);
    for(unsigned i=0; i<nelms; i++) {
      apf::Downward verts;
      for(unsigned j=0; j<nverts; j++) {
        const unsigned mdsIdx = ugridToMdsElmIdx(apfType,j);
        verts[mdsIdx] = lookupVert(r, vtx[i*nverts+j]);
      }
      apf::MeshEntity* elm =
        apf::buildElement(r->mesh, g, apfType, verts);
      assert(elm);
    }
    free(vtx);
    fprintf(stderr, "read %d %s\n", nelms, apf::Mesh::typeName[apfType]);
  }

  void readElms(Reader* r, header* h) {
    readElms(r,h->ntet,apf::Mesh::TET);
    readElms(r,h->npyr,apf::Mesh::PYRAMID);
    readElms(r,h->nprz,apf::Mesh::PRISM);
    readElms(r,h->nhex,apf::Mesh::HEX);
  }

  void freeReader(Reader* r) {
    fclose(r->file);
  }

  void readUgrid(apf::Mesh2* m, const char* filename)
  {
    header hdr;
    Reader r;
    initReader(&r, m, filename);
    readHeader(&r, &hdr);
    hdr.print();
    readNodes(&r, &hdr);
    setNodeIds(&r, &hdr);
    readFacesAndTags(&r,&hdr);
    checkFilePos(&r,&hdr);
    readElms(&r,&hdr);
    setFaceTags(&r,&hdr);
    freeReader(&r);
    m->acceptChanges();
  }

  void getMaxAndAvg(std::set<int>*& cnt, int numparts, unsigned& max, double& avg) {
    for(int i=0; i<numparts; i++) {
      avg += cnt[i].size();
      if( cnt[i].size() > max )
        max = cnt[i].size();
    }
    avg /= numparts;
  }

  void getMaxAndAvg(int* cnt, int numparts, int& max, double& avg) {
    for(int i=0; i<numparts; i++) {
      avg += cnt[i];
      if( cnt[i] > max )
        max = cnt[i];
    }
    avg /= numparts;
  }

  void printPtnStats(apf::Mesh2* m, const char* ufile, const char* ptnFile,
      const double elmWeights[]) {
    header hdr;
    Reader r;
    initReader(&r, m, ufile);
    readHeader(&r, &hdr);
    hdr.print();

    FILE* f = fopen(ptnFile, "r");
    int numparts = 0;
    // 'ptn' is the vertex id to part id array
    int* ptn = new int[hdr.nvtx];
    for(long id=0; id<hdr.nvtx; id++) {
      fscanf(f, "%d", &ptn[id]);
      if( ptn[id] > numparts )
        numparts = ptn[id];
    }
    numparts++; //we want count, not rank
    fclose(f);
    fprintf(stderr, "read ptn for %d parts\n", numparts);

    // 'part[vtx|elm]' counts the number of [vtx|elm] per part
    typedef std::set<int> SetInt;
    SetInt* partvtx = new SetInt[numparts];
    int* partelm = new int[numparts];
    int* partelmW = new int[numparts]; //weighted elm counts
    for(int i=0; i<numparts; i++)
      partelm[i] = partelmW[i] = 0;

    //count number of non-ghosted vtx per part
    for(long id=0; id<hdr.nvtx; id++)
      partvtx[ ptn[id] ].insert(id);
    unsigned maxvtx = 0; double avgvtx = 0;
    getMaxAndAvg(partvtx,numparts,maxvtx,avgvtx);
    double imbvtx = maxvtx / avgvtx;
    fprintf(stderr, "imbvtx %.3f avgvtx %.3f\n", imbvtx, avgvtx);

    readNodes(&r, &hdr); //dummy to advance the fileptr
    readFacesAndTags(&r,&hdr); //dummy to advance the fileptr
    free(r.faceVerts[faceTypeIdx(apf::Mesh::TRIANGLE)]);
    free(r.faceVerts[faceTypeIdx(apf::Mesh::QUAD)]);
    free(r.faceTags[faceTypeIdx(apf::Mesh::TRIANGLE)]);
    free(r.faceTags[faceTypeIdx(apf::Mesh::QUAD)]);
    checkFilePos(&r,&hdr); //sanity check

    SetInt elmparts;

    int types[4] =
      {apf::Mesh::TET, apf::Mesh::PYRAMID, apf::Mesh::PRISM, apf::Mesh::HEX};
    long typeCnt[4] = {hdr.ntet, hdr.npyr, hdr.nprz, hdr.nhex};
    for(int tidx=0; tidx < 4; tidx++) {
      const int apfType = types[tidx];
      const long nelms = typeCnt[tidx];
      const unsigned nverts = apf::Mesh::adjacentCount[apfType][0];
      size_t cnt = nelms*nverts;
      unsigned* vtx = (unsigned*) calloc(cnt,sizeof(unsigned));
      readUnsigneds(r.file, vtx, cnt, r.swapBytes);
      for(long i=0; i<nelms; i++) {
        //determine which parts have a copy of the element
        //  based on who ones the elements bounding vertices
        for(unsigned j=0; j<nverts; j++) {
          const int id = ftnToC(vtx[i*nverts+j]);
          elmparts.insert( ptn[id] );
        }
        assert(elmparts.size() <= nverts);
        //increment the elm per part counts
        APF_ITERATE(SetInt,elmparts,ep) {
          assert(*ep < numparts);
          partelmW[*ep] += elmWeights[apfType];
          partelm[*ep]++;
          for(unsigned j=0; j<nverts; j++) {
            const int id = ftnToC(vtx[i*nverts+j]);
            partvtx[ *ep ].insert(id);
          }
        }
        elmparts.clear();
        assert(!elmparts.size());
      }
      free(vtx);
      fprintf(stderr, "read %lu %s\n", nelms, apf::Mesh::typeName[apfType]);
    }

    //get max and avg vtx and elm per part
    int maxelm = 0; double avgelm = 0;
    int maxelmW = 0; double avgelmW = 0;
    maxvtx = 0; avgvtx = 0;
    getMaxAndAvg(partvtx,numparts,maxvtx,avgvtx);
    getMaxAndAvg(partelm,numparts,maxelm,avgelm);
    getMaxAndAvg(partelmW,numparts,maxelmW,avgelmW);
    imbvtx = maxvtx / avgvtx;
    double imbelm = maxelm / avgelm;
    double imbelmW = maxelmW / avgelmW;
    fprintf(stderr, "imbvtx %.3f imbelmW %.3f imbelm %.3f "
        "avgvtx %.3f avgelmW %.3f avgelm %.3f\n",
        imbvtx, imbelmW, imbelm, avgvtx, avgelmW, avgelm);

    delete [] ptn;
    delete [] partelm;
    delete [] partelmW;
    delete [] partvtx;
  }
}

namespace apf {
  Mesh2* loadMdsFromUgrid(gmi_model* g, const char* filename)
  {
    Mesh2* m = makeEmptyMdsMesh(g, 0, false);
    apf::changeMdsDimension(m, 3);
    readUgrid(m, filename);
    fprintf(stderr,"vtx %lu edge %lu face %lu rgn %lu\n",
        m->count(0), m->count(1), m->count(2), m->count(3));
    return m;
  }
  void printUgridPtnStats(gmi_model* g, const char* ufile, const char* vtxptn,
      const double elmWeights[]) {
    Mesh2* m = makeEmptyMdsMesh(g, 0, false);
    apf::changeMdsDimension(m, 3);
    printPtnStats(m, ufile, vtxptn, elmWeights);
    m->destroyNative();
    apf::destroyMesh(m);
  }
}
