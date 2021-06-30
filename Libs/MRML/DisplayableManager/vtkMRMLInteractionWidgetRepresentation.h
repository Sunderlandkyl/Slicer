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
 * @class   vtkMRMLInteractionWidgetRepresentation
 * @brief   Class for rendering interaction handles
 *
 * This class can display interaction handles in the scene.
 * It plays a similar role to vtkWidgetRepresentation, but it is
 * simplified and specialized for optimal use in Slicer.
 * It state is stored in the associated MRML display node to
 * avoid extra synchronization mechanisms.
 * The representation only observes MRML node changes,
 * it does not directly process any interaction events directly
 * (interaction events are processed by vtkMRMLAbstractWidget,
 * which then modifies MRML nodes).
 *
 * This class (and subclasses) are a type of
 * vtkProp; meaning that they can be associated with a vtkRenderer end
 * embedded in a scene like any other vtkActor.
*
 * @sa
 * vtkMRMLInteractionWidgetRepresentation vtkMRMLAbstractWidget vtkPointPlacer
*/

#ifndef vtkSlicerInteractionRepresentation_h
#define vtkSlicerInteractionRepresentation_h

#include "vtkMRMLDisplayableManagerExport.h"

#include "vtkMRMLAbstractWidgetRepresentation.h"

#include "vtkActor2D.h"
#include "vtkAppendPolyData.h"
#include "vtkArcSource.h"
#include "vtkArrowSource.h"
#include "vtkGlyph3D.h"
#include "vtkLookupTable.h"
#include "vtkPointPlacer.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkSmartPointer.h"
#include "vtkSphereSource.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTensorGlyph.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkTubeFilter.h"
#include <vtkMRMLSliceNode.h>
class vtkMRMLInteractionEventData;
class vtkMRMLDisplayNode;
class vtkMRMLDisplayableNode;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLInteractionWidgetRepresentation : public vtkMRMLAbstractWidgetRepresentation
{
public:

  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLInteractionWidgetRepresentation, vtkMRMLAbstractWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  /// Update the representation from display node
  void UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData = nullptr) override;

  /// Methods to make this class behave as a vtkProp.
  void GetActors(vtkPropCollection*) override;
  void ReleaseGraphicsResources(vtkWindow*) override;
  int RenderOverlay(vtkViewport* viewport) override;
  int RenderOpaqueGeometry(vtkViewport* viewport) override;
  int RenderTranslucentPolygonalGeometry(vtkViewport* viewport) override;
  vtkTypeBool HasTranslucentPolygonalGeometry() override;

  /// Translation, rotation, scaling will happen around this position
  virtual bool GetTransformationReferencePoint(double referencePointWorld[3]);

  /// Return found component type (as vtkMRMLInteractionDisplayNode::ComponentType).
  /// closestDistance2 is the squared distance in display coordinates from the closest position where interaction is possible.
  /// componentIndex returns index of the found component (e.g., if control point is found then control point index is returned).
  virtual void CanInteract(vtkMRMLInteractionEventData* interactionEventData,
    int &foundComponentType, int &foundComponentIndex, double &closestDistance2);

  virtual vtkPointPlacer* GetPointPlacer();

  //@{
  /**
  * Returns true if the representation is displayable in the current view.
  * It takes into account current view node's display node and parent folder's visibility.
  */
  virtual bool IsDisplayable() = 0;
  //@}

  virtual vtkMRMLSliceNode* GetSliceNode();
  virtual void GetSliceToWorldCoordinates(const double slicePos[2], double worldPos[3]);

  virtual void UpdatePlaneFromSliceNode();

  /// Get the axis for the handle specified by the index
  virtual void GetInteractionHandleAxisWorld(int type, int index, double axis[3]);
  /// Get the origin of the interaction handle widget
  virtual void GetInteractionHandleOriginWorld(double origin[3]);

  enum
  {
    InteractionNone,
    InteractionTranslationHandle,
    InteractionRotationHandle,
    InteractionScaleHandle,
  };

  virtual int GetActiveComponentType() = 0;
  virtual void SetActiveComponentType(int type) = 0;

  virtual int GetActiveComponentIndex() = 0;
  virtual void SetActiveComponentIndex(int index) = 0;

  virtual double GetMaximumHandlePickingDistance2();

protected:
  vtkMRMLInteractionWidgetRepresentation();
  ~vtkMRMLInteractionWidgetRepresentation() override;

  class VTK_MRML_DISPLAYABLEMANAGER_EXPORT InteractionPipeline
  {
  public:
    InteractionPipeline();
    virtual ~InteractionPipeline();

    vtkSmartPointer<vtkSphereSource>            AxisRotationHandleSource;
    vtkSmartPointer<vtkArcSource>               AxisRotationArcSource;
    vtkSmartPointer<vtkTubeFilter>              AxisRotationTubeFilter;
    vtkSmartPointer<vtkAppendPolyData>          AxisRotationGlyphSource;
    vtkSmartPointer<vtkPolyData>                RotationHandlePoints;
    vtkSmartPointer<vtkTransformPolyDataFilter> RotationScaleTransform;
    vtkSmartPointer<vtkTensorGlyph>             AxisRotationGlypher;

    vtkSmartPointer<vtkArrowSource>             AxisTranslationGlyphSource;
    vtkSmartPointer<vtkTransformPolyDataFilter> AxisTranslationGlyphTransformer;
    vtkSmartPointer<vtkPolyData>                TranslationHandlePoints;
    vtkSmartPointer<vtkTransformPolyDataFilter> TranslationScaleTransform;
    vtkSmartPointer<vtkGlyph3D>                 AxisTranslationGlypher;

    vtkSmartPointer<vtkSphereSource>            AxisScaleHandleSource;
    vtkSmartPointer<vtkPolyData>                ScaleHandlePoints;
    vtkSmartPointer<vtkTransformPolyDataFilter> ScaleScaleTransform;
    vtkSmartPointer<vtkGlyph3D>                 AxisScaleGlypher;

    vtkSmartPointer<vtkAppendPolyData>          Append;
    vtkSmartPointer<vtkTransformPolyDataFilter> HandleToWorldTransformFilter;
    vtkSmartPointer<vtkTransform>               HandleToWorldTransform;
    vtkSmartPointer<vtkLookupTable>             ColorTable;
    vtkSmartPointer<vtkPolyDataMapper2D>        Mapper;
    vtkSmartPointer<vtkActor2D>                 Actor;
    vtkSmartPointer<vtkProperty2D>              Property;

    vtkSmartPointer<vtkTransformPolyDataFilter> WorldToSliceTransformFilter;
  };

  struct HandleInfo
  {
    HandleInfo(int index, int componentType, double positionWorld[3], double positionLocal[3], double color[4])
      : Index(index)
      , ComponentType(componentType)
    {
    for (int i = 0; i < 3; ++i)
      {
      this->PositionWorld[i] = positionWorld[i];
      }
    this->PositionWorld[3] = 1.0;
    for (int i = 0; i < 3; ++i)
      {
      this->PositionLocal[i] = positionLocal[i];
      }
    this->PositionLocal[3] = 1.0;
    for (int i = 0; i < 4; ++i)
      {
      this->Color[i] = color[i];
      }
    }
    int Index;
    int ComponentType;
    double PositionLocal[4];
    double PositionWorld[4];
    double Color[4];
    bool IsVisible()
      {
      double epsilon = 0.001;
      return this->Color[3] > epsilon;
      }
  };

  /// Get the list of info for all interaction handles
  typedef std::vector<HandleInfo> HandleInfoList;
  virtual HandleInfoList GetHandleInfoList();

  virtual void InitializePipeline();
  virtual void CreateRotationHandles();
  virtual void CreateTranslationHandles();
  virtual void CreateScaleHandles();
  virtual void UpdateHandleColors();

  /// Set the scale of the interaction handles in world coordinates
  virtual void SetWidgetScale(double scale);
  /// Get the color of the specified handle
  /// Type is specified using InteractionType enum
  virtual void GetHandleColor(int type, int index, double color[4]);
  /// Get the opacity of the specified handle
  virtual double GetHandleOpacity(int type, int index);

  /// Get the view plane normal for the widget in world coordinates
  virtual void GetViewPlaneNormal(double normal[3]);

  /// Get the position of the interaction handle in world coordinates
  /// Type is specified using vtkMRMLInteractionDisplayNode::ComponentType
  virtual void GetInteractionHandlePositionWorld(int type, int index, double position[3]);

  // Calculate view size and scale factor
  virtual void UpdateViewScaleFactor();

  virtual void UpdateHandleSize();
  virtual double GetInteractionScale();
  virtual double GetInteractionSize();
  virtual bool GetInteractionSizeAbsolute();

  double StartFadeAngle{ 30 };
  double EndFadeAngle{ 20 };
  double InteractionHandleScaleFactor{ 7.0 };

  double ViewScaleFactorMmPerPixel;
  double ScreenSizePixel; // diagonal size of the screen

  vtkSmartPointer<vtkTransform> WorldToSliceTransform{ nullptr };
  vtkSmartPointer<vtkPlane> SlicePlane{ nullptr };

  // Handle size, specified in renderer world coordinate system.
  // For slice views, renderer world coordinate system is the display coordinate system, so it is measured in pixels.
  // For 3D views, renderer world coordinate system is the Slicer world coordinate system, so it is measured in the
  // scene length unit (typically millimeters).
  double InteractionSize{ 3.0 };

  vtkSmartPointer<vtkPointPlacer> PointPlacer;

  virtual void SetupInteractionPipeline();
  InteractionPipeline* Pipeline;

  /// Update the interaction pipeline
  virtual void UpdateInteractionPipeline();

  double GetViewScaleFactorAtPosition(double positionWorld[3]);

private:
  vtkMRMLInteractionWidgetRepresentation(const vtkMRMLInteractionWidgetRepresentation&) = delete;
  void operator=(const vtkMRMLInteractionWidgetRepresentation&) = delete;
};

#endif
