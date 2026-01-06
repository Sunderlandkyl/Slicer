/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

/**
 * @class   vtkMRMLLabelsWidgetRepresentation
 * @brief   Base class for rendering label text with connecting lines
 *
 * This class provides the base representation for label text actors and
 * their connecting lines to anchor points. Subclasses implement 2D and 3D
 * specific rendering.
 *
 * @sa
 * vtkMRMLAbstractWidgetRepresentation vtkMRMLLabelsWidget
 */

#ifndef __vtkMRMLLabelsWidgetRepresentation_h
#define __vtkMRMLLabelsWidgetRepresentation_h

#include "vtkMRMLAbstractWidgetRepresentation.h"
#include "vtkMRMLDisplayableManagerExport.h"

// MRML includes
#include "vtkMRMLLabelDisplayNode.h"

class vtkMRMLAbstractDisplayableManager;

// VTK includes
#include <vtkActor2D.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkSmartPointer.h>
#include <vtkTextActor.h>

// STD includes
#include <map>
#include <string>
#include <vector>

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLLabelsWidgetRepresentation
  : public vtkMRMLAbstractWidgetRepresentation
{
public:
  vtkTypeMacro(vtkMRMLLabelsWidgetRepresentation, vtkMRMLAbstractWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Update the representation from MRML
  void UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void* callData = nullptr) override;

  /// Methods to make this class behave as a vtkProp
  void GetActors(vtkPropCollection*) override;
  void ReleaseGraphicsResources(vtkWindow*) override;
  int RenderOverlay(vtkViewport* viewport) override;
  int RenderOpaqueGeometry(vtkViewport* viewport) override;
  int RenderTranslucentPolygonalGeometry(vtkViewport* viewport) override;
  vtkTypeBool HasTranslucentPolygonalGeometry() override;

  /// Set the label display node
  virtual void SetLabelDisplayNode(vtkMRMLLabelDisplayNode* displayNode);
  vtkMRMLLabelDisplayNode* GetLabelDisplayNode();

  /// Set the displayable manager (provides GetLabelInfo)
  virtual void SetDisplayableManager(vtkMRMLAbstractDisplayableManager* dm);
  vtkMRMLAbstractDisplayableManager* GetDisplayableManager();

  /// Update all labels from the display node
  virtual void UpdateLabels();

  /// Update label positions (implement collision avoidance)
  virtual void UpdateLabelPositions() = 0;

  /// Update label actor positions and line geometry
  virtual void UpdateLabelActors();

protected:
  vtkMRMLLabelsWidgetRepresentation();
  ~vtkMRMLLabelsWidgetRepresentation() override;

  /// Information about a single label
  struct LabelInfo
  {
    vtkSmartPointer<vtkTextActor> TextActor;
    vtkSmartPointer<vtkPolyDataMapper2D> LineMapper;
    vtkSmartPointer<vtkActor2D> LineActor;
    vtkSmartPointer<vtkPolyData> LinePolyData;
    vtkMRMLLabelDisplayNode* DisplayNode;
    int LabelIndex{ 0 };
    std::string Key;           // nodeID#index
    double AnchorPosition[3];  // World coordinates
    double DisplayPosition[2]; // Display coordinates
    int AssignedPosition[2];   // Final position after collision avoidance

    // Caching for performance
    std::string CachedText;
    int CachedFontSize{0};
    double CachedTextWidth{0.0};
    double CachedTextHeight{0.0};
    int CachedViewportSize[2]{0,0};
    bool SizeDirty{true};
    bool StyleDirty{true};

    int PrevAssignedPosition[2]{INT_MIN, INT_MIN};
    double PrevAnchorDisplay[2]{std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::quiet_NaN()};
  };

  typedef std::map<std::string, LabelInfo> LabelsMapType;
  LabelsMapType Labels;

  /// Add a label to the representation
  virtual void AddLabel(vtkMRMLLabelDisplayNode* displayNode);

  /// Update a label's properties
  virtual void UpdateLabel(vtkMRMLLabelDisplayNode* displayNode);

  /// Remove a label from the representation
  virtual void RemoveLabel(vtkMRMLLabelDisplayNode* displayNode);

  /// Remove all labels
  virtual void RemoveAllLabels();

  /// Convert world coordinates to display coordinates (subclass implements)
  virtual void WorldToDisplay(const double worldPos[3], double displayPos[2]) = 0;

  /// Update line geometry for a label
  virtual void UpdateLineGeometry(LabelInfo& label);

  /// Collision avoidance helper
  virtual void AdjustLabelsForCollision();

  /// Check collision between two bounding boxes
  bool CheckCollision(const double pos1[2], const double size1[2],
                      const double pos2[2], const double size2[2]);

  vtkWeakPointer<vtkMRMLLabelDisplayNode> LabelDisplayNode;
  vtkWeakPointer<vtkMRMLAbstractDisplayableManager> DisplayableManager;

private:
  vtkMRMLLabelsWidgetRepresentation(const vtkMRMLLabelsWidgetRepresentation&) = delete;
  void operator=(const vtkMRMLLabelsWidgetRepresentation&) = delete;
};

#endif
