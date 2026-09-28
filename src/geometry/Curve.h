
#pragma once
#include <memory>
#include <vector>
#include "geometry/linalg.h"
class Curve
{
public:
  virtual ~Curve() = default;
  int start, end;
  virtual void display(const std::vector<Vector3d>& vertices);
  virtual void reverse(void);
  /*! Whether two curve records are one curve, for deduplication. A straight
   * curve is its two vertex indices and is never merged here. */
  [[nodiscard]] virtual bool sameAs(const Curve& other) const;
  virtual int pointMember(std::vector<Vector3d>& vertices, Vector3d pt);

  /*! Independent copy, so transforming one geometry never mutates a curve
   * another geometry still shares. */
  [[nodiscard]] virtual std::shared_ptr<Curve> clone() const;

  /*! Move the curve with the geometry it describes. Returns false when the
   * result can no longer be represented, in which case the caller drops it. */
  virtual bool transform(const Transform3d& mat, const Similarity& similarity);
};

class ArcCurve : public Curve
{
public:
  ArcCurve(Vector3d center, Vector3d normdir, double r);
  void display(const std::vector<Vector3d>& vertices);
  void reverse(void);
  /*! The same vertices, and one circle at machine precision: parallel planes,
   * and three points of each circle on the other (recognition/Exact.h). */
  [[nodiscard]] bool sameAs(const Curve& other) const override;
  double calcAngle(Vector3d refdir, Vector3d dir, Vector3d normdir);
  virtual int pointMember(std::vector<Vector3d>& vertices, Vector3d pt);

  [[nodiscard]] std::shared_ptr<Curve> clone() const override;
  bool transform(const Transform3d& mat, const Similarity& similarity) override;

  double r;
  Vector3d center, normdir;
};
