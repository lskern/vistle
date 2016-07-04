//-------------------------------------------------------------------------
// RECTILINEAR GRID CLASS H
// *
// * Rectilinear Grid Container Object
//-------------------------------------------------------------------------

#include "rectilineargrid.h"
#include "assert.h"

namespace vistle {

// CONSTRUCTOR
//-------------------------------------------------------------------------
RectilinearGrid::RectilinearGrid(const Index NumElements_x, const Index NumElements_y, const Index NumElements_z, const Meta &meta)
    : RectilinearGrid::Base(RectilinearGrid::Data::create(NumElements_x, NumElements_y, NumElements_z, meta)) {
    refreshImpl();
}

// REFRESH IMPL
//-------------------------------------------------------------------------
void RectilinearGrid::refreshImpl() const {
    const Data *d = static_cast<Data *>(m_data);
    for (int c=0; c<3; ++c) {
        if (d && d->coords[c].valid()) {
            m_numDivisions[c] = d->coords[c]->size();
            m_coords[c] = (d && d->coords[c].valid()) ? d->coords[c]->data() : nullptr;
        } else {
            m_numDivisions[c] = 0;
            m_coords[c] = nullptr;
        }
    }
}

// CHECK IMPL
//-------------------------------------------------------------------------
bool RectilinearGrid::checkImpl() const {

    for (int c=0; c<3; ++c) {
        V_CHECK(d()->coords[c]->check());
    }

   return true;
}

// IS EMPTY
//-------------------------------------------------------------------------
bool RectilinearGrid::isEmpty() const {

   return (getNumDivisions(0) == 0 || getNumDivisions(1) == 0 || getNumDivisions(2) == 0);
}

std::pair<Vector, Vector> RectilinearGrid::getBounds() const {

    return std::make_pair(Vector(m_coords[0][0], m_coords[1][0], m_coords[2][0]),
            Vector(m_coords[0][m_numDivisions[0]-1], m_coords[1][m_numDivisions[1]-1], m_coords[2][m_numDivisions[2]-1]));
}

Index RectilinearGrid::findCell(const Vector &point, bool acceptGhost) const {

    for (int c=0; c<3; ++c) {
        if (point[c] < m_coords[c][0])
            return InvalidIndex;
        if (point[c] > m_coords[c][m_numDivisions[c]-1])
            return InvalidIndex;
    }

    Index n[3] = {0, 0, 0};
    for (int c=0; c<3; ++c) {
        for (Index i=0; i<m_numDivisions[c]; ++i) {
            if (m_coords[c][i] > point[c])
                break;
            n[c] = i;
        }
    }

    Index el = cellIndex(n, m_numDivisions);
    if (acceptGhost || !isGhostCell(el))
        return el;
    return InvalidIndex;

}

bool RectilinearGrid::inside(Index elem, const Vector &point) const {

    std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
    for (int c=0; c<3; ++c) {
        Scalar x0 = m_coords[c][n[c]], x1 = m_coords[c][n[c]+1];
        if (point[c] < x0)
            return false;
        if (point[c] > x1)
            return false;
    }

    return true;
}

GridInterface::Interpolator RectilinearGrid::getInterpolator(Index elem, const Vector &point, DataBase::Mapping mapping, GridInterface::InterpolationMode mode) const {

   vassert(inside(elem, point));

#ifdef INTERPOL_DEBUG
   if (!inside(elem, point)) {
      return Interpolator();
   }
#endif

   if (mapping == DataBase::Element) {
       std::vector<Scalar> weights(1, 1.);
       std::vector<Index> indices(1, elem);

       return Interpolator(weights, indices);
   }

   std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
   std::array<Index,8> cl = cellVertices(elem, m_numDivisions);

   Vector corner0(m_coords[0][n[0]], m_coords[1][n[1]], m_coords[2][n[2]]);
   Vector corner1(m_coords[0][n[0]+1], m_coords[1][n[1]+1], m_coords[2][n[2]+1]);
   const Vector diff = point-corner0;
   const Vector size = corner1-corner0;

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
       Vector ss = diff;
       for (int c=0; c<3; ++c) {
           ss[c] /= size[c];
       }
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
          Vector ss = diff;
          int nearest=0;
          for (int c=0; c<3; ++c) {
              nearest <<= 1;
              ss[c] /= size[c];
              if (ss[c] < 0.5)
                  nearest |= 1;
          }
          indices[0] = cl[nearest];
      }
   }

   return Interpolator(weights, indices);

}

// DATA OBJECT - CONSTRUCTOR FROM NAME & META
//-------------------------------------------------------------------------
RectilinearGrid::Data::Data(const Index NumElements_x, const Index NumElements_y, const Index NumElements_z, const std::string & name, const Meta &meta)
    : RectilinearGrid::Base::Data(Object::RECTILINEARGRID, name, meta) {
    coords[0].construct(NumElements_x + 1);
    coords[1].construct(NumElements_y + 1);
    coords[2].construct(NumElements_z + 1);
}

// DATA OBJECT - CONSTRUCTOR FROM DATA OBJECT AND NAME
//-------------------------------------------------------------------------
RectilinearGrid::Data::Data(const RectilinearGrid::Data &o, const std::string &n)
    : RectilinearGrid::Base::Data(o, n) {
    for (int c=0; c<3; ++c) {
        coords[c] = o.coords[c];
    }
}

// DATA OBJECT - DESTRUCTOR
//-------------------------------------------------------------------------
RectilinearGrid::Data::~Data() {

}

// DATA OBJECT - CREATE FUNCTION
//-------------------------------------------------------------------------
RectilinearGrid::Data * RectilinearGrid::Data::create(const Index NumElements_x, const Index NumElements_y, const Index NumElements_z, const Meta &meta) {

    const std::string name = Shm::the().createObjectId();
    Data *p = shm<Data>::construct(name)(NumElements_x, NumElements_y, NumElements_z, name, meta);
    publish(p);

    return p;
}

// MACROS
//-------------------------------------------------------------------------
V_OBJECT_TYPE(RectilinearGrid, Object::RECTILINEARGRID)
V_OBJECT_CTOR(RectilinearGrid)

} // namespace vistle
