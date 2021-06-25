/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

/**
 * @class   vtkMRMLInteractionWidgetRepresentation2D
 * @brief   Default representation for the slicer interaction widget
 *
 * This class provides the default concrete representation for the
 * vtkMRMLAbstractWidget. See vtkMRMLAbstractWidget
 * for details.
 * @sa
 * vtkMRMLInteractionWidgetRepresentation2D vtkMRMLAbstractWidget
*/

#ifndef vtkMRMLInteractionWidgetRepresentation2D_h
#define vtkMRMLInteractionWidgetRepresentation2D_h

#include "vtkMRMLDisplayableManagerExport.h"
#include "vtkMRMLInteractionWidgetRepresentation.h"

#include "vtkMRMLSliceNode.h"

class vtkActor2D;
class vtkDiscretizableColorTransferFunction;
class vtkGlyph2D;
class vtkLabelPlacementMapper;
class vtkPlane;
class vtkPolyDataMapper2D;
class vtkProperty2D;

class vtkMRMLInteractionEventData;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLInteractionWidgetRepresentation2D : public vtkMRMLInteractionWidgetRepresentation
{
public:
  /**
   * Instantiate this class.
   */
  static vtkMRMLInteractionWidgetRepresentation2D* New();

  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLInteractionWidgetRepresentation2D, vtkMRMLInteractionWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  /// Position is displayed (slice) position
  void CanInteract(vtkMRMLInteractionEventData* interactionEventData,
    int &foundComponentType, int &foundComponentIndex, double &closestDistance2) override;

  /// Check if interaction with the transformation handles is possible
  virtual void CanInteractWithHandles(vtkMRMLInteractionEventData* interactionEventData,
    int& foundComponentType, int& foundComponentIndex, double& closestDistance2);

  /// Subclasses of vtkMRMLInteractionWidgetRepresentation2D must implement these methods. These
  /// are the methods that the widget and its representation use to
  /// communicate with each other.
  void UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData=nullptr) override;

  /// Methods to make this class behave as a vtkProp.
  void GetActors(vtkPropCollection *) override;
  void ReleaseGraphicsResources(vtkWindow *) override;
  int RenderOverlay(vtkViewport *viewport) override;
  int RenderOpaqueGeometry(vtkViewport *viewport) override;
  int RenderTranslucentPolygonalGeometry(vtkViewport *viewport) override;
  vtkTypeBool HasTranslucentPolygonalGeometry() override;


  void GetSliceToWorldCoordinates(const double[2], double[3]);
  void GetWorldToSliceCoordinates(const double worldPos[3], double slicePos[2]);

  void UpdateInteractionPipeline() override;

protected:
  vtkMRMLInteractionWidgetRepresentation2D();
  ~vtkMRMLInteractionWidgetRepresentation2D() override;

  /// Reimplemented for 2D specific mapper/actor settings
  void SetupInteractionPipeline() override;

    /// Get MRML view node as slice view node
  vtkMRMLSliceNode *GetSliceNode();

  void UpdatePlaneFromSliceNode();

  void UpdateViewScaleFactor() override;
  void UpdateHandleSize() override;

  // Return squared distance of maximum distance for picking a control point,
  // in pixels.
  double GetMaximumHandlePickingDistance2();

  /// Convert display to world coordinates
  void GetWorldToDisplayCoordinates(double r, double a, double s, double * displayCoordinates);
  void GetWorldToDisplayCoordinates(double * worldCoordinates, double * displayCoordinates);

  vtkSmartPointer<vtkIntArray> PointsVisibilityOnSlice;
  bool                         CenterVisibilityOnSlice = { false };
  bool                         AnyPointVisibilityOnSlice = { false };  // at least one point is visible

  vtkSmartPointer<vtkTransform> WorldToSliceTransform;
  vtkSmartPointer<vtkPlane> SlicePlane;

  void GetViewPlaneNormal(double viewPlaneNormal[3]) override;

  class VTK_MRML_DISPLAYABLEMANAGER_EXPORT InteractionPipeline2D : public InteractionPipeline
  {
  public:
    InteractionPipeline2D();
    virtual ~InteractionPipeline2D() = default;

    vtkSmartPointer<vtkTransformPolyDataFilter> WorldToSliceTransformFilter;
  };

private:
  vtkMRMLInteractionWidgetRepresentation2D(const vtkMRMLInteractionWidgetRepresentation2D&) = delete;
  void operator=(const vtkMRMLInteractionWidgetRepresentation2D&) = delete;
};

#endif
