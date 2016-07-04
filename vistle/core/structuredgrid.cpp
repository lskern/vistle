//-------------------------------------------------------------------------
// STRUCTURED GRID CLASS H
// *
// * Structured Grid Container Object
//-------------------------------------------------------------------------

#include "structuredgrid.h"
#include "unstr.h" // for hexahedron topology
#include <core/assert.h>
#include "cellinterpolation.h"

namespace vistle {

// CONSTRUCTOR
//-------------------------------------------------------------------------
StructuredGrid::StructuredGrid(const Index numVert_x, const Index numVert_y, const Index numVert_z, const Meta &meta)
    : StructuredGrid::Base(StructuredGrid::Data::create(numVert_x, numVert_y, numVert_z, meta)) {
    refreshImpl();

}

// REFRESH IMPL
//-------------------------------------------------------------------------
void StructuredGrid::refreshImpl() const {
    const Data *d = static_cast<Data *>(m_data);

    for (int c=0; c<3; ++c) {
        if (d && d->x[c].valid()) {
            m_numDivisions[c] = (*d->numDivisions)[c];
        } else {
            m_numDivisions[c] = 0;
        }
    }
}

// CHECK IMPL
//-------------------------------------------------------------------------
bool StructuredGrid::checkImpl() const {

   for (int c=0; c<3; ++c)
       V_CHECK(d()->x[c]->check());

   return true;
}

// IS EMPTY
//-------------------------------------------------------------------------
bool StructuredGrid::isEmpty() const {

   return Base::isEmpty();
}

std::pair<Vector, Vector> StructuredGrid::getBounds() const {
#if 0
   if (hasCelltree()) {
      const auto &ct = getCelltree();
      return std::make_pair(Vector(ct->min()), Vector(ct->max()));
   }
#endif

   return Base::getMinMax();
}

Index StructuredGrid::findCell(const Vec::Vector &point, bool acceptGhost) const {

#if 0
   if (hasCelltree()) {

      PointVisitationFunctor<Scalar, Index> nodeFunc(point);
      PointInclusionFunctor<Scalar, Index> elemFunc(this, point);
      getCelltree()->traverse(nodeFunc, elemFunc);
      if (acceptGhost ||!isGhostCell(elemFunc.cell))
         return elemFunc.cell;
      else
         return InvalidIndex;
   }
#endif

   Index size = getNumElements();
   for (Index i=0; i<size; ++i) {
      if (acceptGhost || !isGhostCell(i)) {
         if (inside(i, point))
            return i;
      }
   }
   return InvalidIndex;
}

bool StructuredGrid::inside(Index elem, const Vec::Vector &point) const {

    const Scalar *x = &this->x()[0];
    const Scalar *y = &this->y()[0];
    const Scalar *z = &this->z()[0];

    std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
    std::array<Index,8> cl = cellVertices(elem, m_numDivisions);
    Vector corners[8];
    for (int i=0; i<8; ++i) {
        corners[i][0] = x[cl[i]];
        corners[i][1] = y[cl[i]];
        corners[i][2] = z[cl[i]];
    }

    UnstructuredGrid::Type type = UnstructuredGrid::HEXAHEDRON;
    const auto numFaces = UnstructuredGrid::NumFaces[type];
    const auto &faces = UnstructuredGrid::FaceVertices[type];
    const auto &sizes = UnstructuredGrid::FaceSizes[type];
    for (int f=0; f<numFaces; ++f) {
        Vector v0 = corners[faces[f][0]];
        Vector edge1 = corners[faces[f][1]];
        edge1 -= v0;
        Vector n(0, 0, 0);
        for (unsigned i=2; i<sizes[f]; ++i) {
            Vector edge = corners[faces[f][i]];
            edge -= v0;
            n += edge1.cross(edge);
        }

        //std::cerr << "normal: " << n.transpose() << ", v0: " << v0.transpose() << ", rel: " << (point-v0).transpose() << ", dot: " << n.dot(point-v0) << std::endl;

        if (n.dot(point-v0) > 0)
            return false;
    }
    return true;
}

GridInterface::Interpolator StructuredGrid::getInterpolator(Index elem, const Vec::Vector &point, DataBase::Mapping mapping, GridInterface::InterpolationMode mode) const {

   vassert(inside(elem, point));

#ifdef INTERPOL_DEBUG
   if (!inside(elem, point)) {
      return Interpolator();
   }
#endif

   if (mapping == Element) {
       std::vector<Scalar> weights(1, 1.);
       std::vector<Index> indices(1, elem);

       return Interpolator(weights, indices);
   }

   std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
   std::array<Index,8> cl = cellVertices(elem, m_numDivisions);

   const Scalar *x[3] = { &this->x()[0], &this->y()[0], &this->z()[0] };
   Vector corners[8];
   for (int i=0; i<8; ++i) {
       corners[i][0] = x[0][cl[i]];
       corners[i][1] = x[1][cl[i]];
       corners[i][2] = x[2][cl[i]];
   }

   const Index nvert = 8;
   std::vector<Index> indices((mode==Linear || mode==Mean) ? nvert : 1);
   std::vector<Scalar> weights((mode==Linear || mode==Mean) ? nvert : 1);

   if (mode == Mean) {
       const Scalar w = Scalar(1)/nvert;
       for (Index i=0; i<nvert; ++i) {
           indices[i] = cl[i];
           weights[i] = w;
       }
   } else if (mode == Linear) {
       vassert(nvert == 8);
       const Vector ss = trilinearInverse(point, corners);
       weights[0] = (1-ss[0])*(1-ss[1])*(1-ss[2]);
       weights[1] = ss[0]*(1-ss[1])*(1-ss[2]);
       weights[2] = ss[0]*ss[1]*(1-ss[2]);
       weights[3] = (1-ss[0])*ss[1]*(1-ss[2]);
       weights[4] = (1-ss[0])*(1-ss[1])*ss[2];
       weights[5] = ss[0]*(1-ss[1])*ss[2];
       weights[6] = ss[0]*ss[1]*ss[2];
       weights[7] = (1-ss[0])*ss[1]*ss[2];
   }

   if (mode != Linear && mode != Mean) {
      weights[0] = 1;

      if (mode == First) {
         indices[0] = cl[0];
      } else if(mode == Nearest) {
         Scalar mindist = std::numeric_limits<Scalar>::max();

         for (Index i=0; i<nvert; ++i) {
            const Index k = cl[i];
            const Vector3 vert(x[0][k], x[1][k], x[2][k]);
            const Scalar dist = (point-vert).squaredNorm();
            if (dist < mindist)
               indices[0] = k;
         }
      }
   }

   return Interpolator(weights, indices);
}


// DATA OBJECT - CONSTRUCTOR FROM NAME & META
//-------------------------------------------------------------------------
StructuredGrid::Data::Data(const Index numVert_x, const Index numVert_y, const Index numVert_z, const std::string & name, const Meta &meta)
    : StructuredGrid::Base::Data(Object::STRUCTUREDGRID, name, meta)
{
    numDivisions.construct(3);
    (*numDivisions)[0] = numVert_x;
    (*numDivisions)[1] = numVert_y;
    (*numDivisions)[2] = numVert_z;

    const Index numCoords = numVert_x * numVert_y * numVert_z;
    // construct ShmVectors
    for (int c=0; c<3; ++c) {
        x[c].construct(numCoords);
    }
}

// DATA OBJECT - CONSTRUCTOR FROM DATA OBJECT AND NAME
//-------------------------------------------------------------------------
StructuredGrid::Data::Data(const StructuredGrid::Data &o, const std::string &n)
    : StructuredGrid::Base::Data(o, n)
    , numDivisions(o.numDivisions) {
    for (int c=0; c<3; ++c)
        x[c] = o.x[c];
}

// DATA OBJECT - DESTRUCTOR
//-------------------------------------------------------------------------
StructuredGrid::Data::~Data() {

}

// DATA OBJECT - CREATE FUNCTION
//-------------------------------------------------------------------------
StructuredGrid::Data * StructuredGrid::Data::create(const Index numVert_x, const Index numVert_y, const Index numVert_z, const Meta &meta) {

    // construct shm data
    const std::string name = Shm::the().createObjectId();
    Data *p = shm<Data>::construct(name)(numVert_x, numVert_y, numVert_z, name, meta);
    publish(p);

    return p;
}

// MACROS
//-------------------------------------------------------------------------
V_OBJECT_TYPE(StructuredGrid, Object::STRUCTUREDGRID)
V_OBJECT_CTOR(StructuredGrid)

} // namespace vistle
