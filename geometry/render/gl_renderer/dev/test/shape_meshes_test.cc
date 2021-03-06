#include "drake/geometry/render/gl_renderer/dev/shape_meshes.h"

#include <limits>
#include <sstream>

#include <gtest/gtest.h>

#include "drake/common/eigen_types.h"
#include "drake/common/find_resource.h"
#include "drake/common/test_utilities/expect_throws_message.h"

namespace drake {
namespace geometry {
namespace render {
namespace internal {
namespace {

GTEST_TEST(LoadMeshFromObjTest, ErrorModes) {
  {
    // Case: Vertices only reports no faces found.
    std::stringstream in_stream("v 1 2 3");
    DRAKE_EXPECT_THROWS_MESSAGE(LoadMeshFromObj(&in_stream), std::runtime_error,
                                "The OBJ data appears to have no faces.*");
  }
  {
    // Case: Not an obj in any way reports as no faces.
    std::stringstream in_stream("Not an obj\njust some\nmeaningles text.\n");
    DRAKE_EXPECT_THROWS_MESSAGE(
        LoadMeshFromObj(&in_stream), std::runtime_error,
        "The OBJ data appears to have no faces.* might not be an OBJ file");
  }
}

// Simply confirms that the filename variant of LoadMeshFromObj successfully
// dispatches files or errors, as appropriate. The actual parsing functionality
// is parsed via the stream interface.
GTEST_TEST(LoadMeshFromObjTest, ReadingFile) {
  const std::string filename =
      FindResourceOrThrow("drake/systems/sensors/test/models/meshes/box.obj");

  auto [vertices, indices] = LoadMeshFromObj(filename);
  EXPECT_EQ(vertices.rows(), 8);
  EXPECT_EQ(indices.rows(), 12);

  DRAKE_EXPECT_THROWS_MESSAGE(LoadMeshFromObj("Bad file name"),
                              std::runtime_error,
                              "Cannot load the obj file 'Bad file name'");
}

// Confirms that non-triangular faces get triangulated.
GTEST_TEST(LoadMeshFromObjTest, TriangulatePolygons) {
  /*
             o 4
            ╱  ╲            A five-sided polygon and a four-sided polygon
           ╱    ╲           should be triangulated into three and two
          ╱      ╲          triangles respectively. But with the same
         ╱        ╲         vertices.
        ╱          ╲
     5 o            o 3
        ╲          ╱
         ╲        ╱
       1  o──────o 2
          │      │
          o──────o
          6      7
  */
  std::stringstream in_stream(R"_(
  v -1 -1 0
  v 1 -1 0
  v 2 1 0
  v 0 2 0
  v -2 1 0
  v -1 -2 0
  v 1 -2 0
  f 1 2 3 4 5
  f 6 7 2 1
  )_");
  auto [vertices, indices] = LoadMeshFromObj(&in_stream);
  EXPECT_EQ(vertices.rows(), 7);
  EXPECT_EQ(indices.rows(), 5);
}

// Geometry already triangulated gets preserved.
GTEST_TEST(LoadMeshFromObjTest, PreserveTriangulation) {
  /*
             o 4
            ╱  ╲
           ╱    ╲
          ╱      ╲
         ╱        ╲
        ╱          ╲       Note: Ascii art makes drawing the following edges
     5 o────────────o 3    nearly impossible.
        ╲          ╱
         ╲        ╱        Connect 2 to 5 to form two triangles.
       1  o──────o 2
          │      │         Connect 1 to 7 to form two triangles.
          o──────o
          6      7
  */
  std::stringstream in_stream(R"_(
  v -1 -1 0
  v 1 -1 0
  v 2 1 0
  v 0 2 0
  v -2 1 0
  v -1 -2 0
  v 1 -2 0
  f 1 2 5
  f 2 3 5
  f 3 4 5
  f 1 6 7
  f 1 7 2
  )_");
  auto [vertices, indices] = LoadMeshFromObj(&in_stream);
  EXPECT_EQ(vertices.rows(), 7);
  EXPECT_EQ(indices.rows(), 5);
}

// Confirms that no mesh optimization takes place, e.g., unreferenced vertices
// are still included and duplicate vertices are not merged.
GTEST_TEST(LoadMeshFromObjTest, NoMeshOptimization) {
  /*

        4 o───────o 3
          │     ╱ │
          │   ╱   │   o 5
          │ ╱     │
    1, 6  o───────o 2

  */
  std::stringstream in_stream(R"_(
  v -1 -1 0  # First four corners form a box around the origin.
  v 1 -1 0
  v 1 1 0
  v -1 1 0
  v 3 0 0    # Unreferenced vertex to the right of the box
  v -1 -1 0  # Duplpicate of vertex 1
  f 1 2 3
  f 6 3 4
  )_");
  auto [vertices, indices] = LoadMeshFromObj(&in_stream);
  EXPECT_EQ(vertices.rows(), 6);
  EXPECT_EQ(indices.rows(), 2);
}

// Computes the normal to the indicated triangle whose magnitude is twice the
// triangle's area. I.e., for triangle (A, B, C), computes: (B - A) X (C - A).
Vector3<GLfloat> normal(const VertexBuffer& vertices, const IndexBuffer& tris,
                        int tri_index) {
  const auto& a = vertices.block<1, 3>(tris(tri_index, 0), 0);
  const auto& b = vertices.block<1, 3>(tris(tri_index, 1), 0);
  const auto& c = vertices.block<1, 3>(tris(tri_index, 2), 0);
  return (b - a).cross(c - a);
}

// Computes the area of the indicated triangle.
GLfloat area(const VertexBuffer& vertices, const IndexBuffer& tris,
             int tri_index) {
  return normal(vertices, tris, tri_index).norm() * 0.5;
}

// Computes the normal for the triangle (implied by its winding).
Vector3<GLfloat> unit_normal(const VertexBuffer& vertices,
                             const IndexBuffer& tris, int tri_index) {
  return normal(vertices, tris, tri_index).normalized();
}

// Computes the centroid of the indicated triangle.
Vector3<GLfloat> centroid(const VertexBuffer& vertices, const IndexBuffer& tris,
                          int tri_index) {
  return (vertices.block<1, 3>(tris(tri_index, 0), 0) +
          vertices.block<1, 3>(tris(tri_index, 1), 0) +
          vertices.block<1, 3>(tris(tri_index, 2), 0)) /
         3.f;
}

// Simply confirm that the utility functions above report good values.
GTEST_TEST(ShapeMeshesTest, ConfirmUtilities) {
  VertexBuffer vertices(3, 3);
  /*
            │╲ (0, 1.5, 0)        Right isoceles triangle with legs of length
            │ ╲                   1.5, lying on the z = 0 plane.
            │  ╲                     area = 1.5 * 1.5 / 2 = 2.25 / 2 = 1.125
            │   ╲                    unit normal = <0, 0, 1>
            │    ╲                   normal = <0, 0, 2.25>
   (0, 0, 0)└─────  (1.5, 0, 0)      centroid = (0.5, 0.5, 0)
  */
  vertices.block<1, 3>(0, 0) << 0, 0, 0;
  vertices.block<1, 3>(1, 0) << 1.5, 0, 0;
  vertices.block<1, 3>(2, 0) << 0, 1.5, 0;
  IndexBuffer indices(1, 3);
  indices.block<1, 3>(0, 0) << 0, 1, 2;
  EXPECT_EQ(normal(vertices, indices, 0), Vector3<GLfloat>(0, 0, 2.25));
  EXPECT_EQ(unit_normal(vertices, indices, 0), Vector3<GLfloat>(0, 0, 1));
  EXPECT_EQ(area(vertices, indices, 0), 1.125);
  EXPECT_EQ(centroid(vertices, indices, 0), Vector3<GLfloat>(0.5f, 0.5f, 0));
}

GTEST_TEST(MakeLongLatUnitSphere, ConfirmSphere) {
  const GLfloat kEps = std::numeric_limits<GLfloat>::epsilon();

  // Closed form solution for surface area of unit sphere.
  const GLfloat kIdealArea = static_cast<GLfloat>(4 * M_PI);  // 4πR², R = 1.

  float prev_area = 0;
  for (int resolution : {3, 10, 20, 40}) {
    auto [vertices, indices] = MakeLongLatUnitSphere(resolution, resolution);

    // Confirm area converges towards (but not above) ideal area.
    float total_area = 0;
    for (int t = 0; t < indices.rows(); ++t) {
      const auto a = area(vertices, indices, t);
      total_area += a;
    }
    EXPECT_GT(total_area, prev_area) << "for resolution " << resolution;
    EXPECT_LE(total_area, kIdealArea) << "for resolution " << resolution;
    prev_area = total_area;

    // All vertices lie on the unit sphere.
    for (int v_i = 0; v_i < vertices.rows(); ++v_i) {
      const auto& v = vertices.block<1, 3>(v_i, 0);
      EXPECT_NEAR(v.norm(), 1.f, kEps)
          << "for resolution: " << resolution << " and vertex " << v_i;
    }

    // All face normals point outward
    for (int t = 0; t < indices.rows(); ++t) {
      Vector3<GLfloat> c = centroid(vertices, indices, t);
      Vector3<GLfloat> n = normal(vertices, indices, t);
      // If the winding were backwards, this dot product would be negative.
      EXPECT_GT(n.dot(c), 0)
          << "for resolution: " << resolution << " and triangle " << t;
    }
  }

  /* These tests are merely suggestive; they don't guarantee a correct sphere
   mesh. We *might* consider confirming there are no holes in the mesh. In
   practice that may not be necessary; those kinds of artifacts would be
   immediately apparent in rendered images. Defer those heavyweight types of
   tests until there's a proven bug. */
}

}  // namespace
}  // namespace internal
}  // namespace render
}  // namespace geometry
}  // namespace drake
