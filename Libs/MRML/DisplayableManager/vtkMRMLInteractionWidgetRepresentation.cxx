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

// VTK includes
#include "vtkAppendPolyData.h"
#include "vtkArcSource.h"
#include "vtkArrowSource.h"
#include "vtkCamera.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkFocalPlanePointPlacer.h"
#include "vtkGlyph3D.h"
#include "vtkLine.h"
#include "vtkLineSource.h"
#include "vtkLookupTable.h"
#include "vtkMRMLSliceNode.h"
#include "vtkMRMLViewNode.h"
#include <vtkPlane.h>
#include "vtkPointData.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkRenderer.h"
#include <vtkRenderWindow.h>
#include "vtkSphereSource.h"
#include "vtkStringArray.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTensorGlyph.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkTubeFilter.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>

#include <vtkMRMLInteractionWidgetRepresentation.h>

//----------------------------------------------------------------------
static const double INTERACTION_HANDLE_RADIUS = 0.0625;
static const double INTERACTION_HANDLE_DIAMETER = INTERACTION_HANDLE_RADIUS * 2.0;
static const double INTERACTION_HANDLE_ROTATION_ARC_TUBE_RADIUS = INTERACTION_HANDLE_RADIUS * 0.4;
static const double INTERACTION_HANDLE_ROTATION_ARC_RADIUS = 0.80;
static const double INTERACTION_TRANSLATION_HANDLE_RADIUS = INTERACTION_HANDLE_RADIUS * 0.75;
static const double INTERACTION_TRANSLATION_HANDLE_DIAMETER = INTERACTION_TRANSLATION_HANDLE_RADIUS * 2.0;
static const double INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS = INTERACTION_TRANSLATION_HANDLE_RADIUS * 0.5;

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::vtkMRMLInteractionWidgetRepresentation()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000;

  this->NeedToRender = false;

  this->PointPlacer = vtkSmartPointer<vtkFocalPlanePointPlacer>::New();

  this->AlwaysOnTop = false;

  this->Pipeline = nullptr;

  this->SlicePlane = vtkSmartPointer<vtkPlane>::New();
  this->WorldToSliceTransform = vtkSmartPointer<vtkTransform>::New();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetupInteractionPipeline()
{
  this->Pipeline = new InteractionPipeline();
  if (this->GetSliceNode())
    {
    this->Pipeline->WorldToSliceTransformFilter->SetInputConnection(this->Pipeline->HandleToWorldTransformFilter->GetOutputPort());
    this->Pipeline->WorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);
    this->Pipeline->Mapper->SetInputConnection(this->Pipeline->WorldToSliceTransformFilter->GetOutputPort());
    this->Pipeline->Mapper->SetTransformCoordinate(nullptr);
    }

  this->InitializePipeline();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::~vtkMRMLInteractionWidgetRepresentation()
{
  // Force deleting variables to prevent circular dependency keeping objects alive
  this->PointPlacer = nullptr;

  if (this->Pipeline != nullptr)
    {
    delete this->Pipeline;
    this->Pipeline = nullptr;
    }
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::PrintSelf(ostream& os,
                                                      vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Point Placer: " << this->PointPlacer << "\n";
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetMaximumHandlePickingDistance2()
{
  double maximumHandlePickingDistance = this->InteractionSize / 2.0 + this->PickingTolerance * this->ScreenScaleFactor;
  return maximumHandlePickingDistance * maximumHandlePickingDistance;
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = InteractionNone;
  vtkMRMLAbstractViewNode* viewNode = this->GetViewNode();
  if (!viewNode || !this->GetVisibility() || !interactionEventData)
    {
    return;
    }

  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system
  foundComponentIndex = -1;

  double maxPickingDistanceFromControlPoint2 = this->GetMaximumHandlePickingDistance2();
  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  if (interactionEventData->IsDisplayPositionValid())
    {
    const int* displayPosition = interactionEventData->GetDisplayPosition();
    displayPosition3[0] = static_cast<double>(displayPosition[0]);
    displayPosition3[1] = static_cast<double>(displayPosition[1]);
    }
  else if (!interactionEventData->IsWorldPositionValid())
    {
    return;
    }

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (sliceNode)
    {
    double handleDisplayPos[4] = { 0.0, 0.0, 0.0, 1.0 };

    vtkNew<vtkMatrix4x4> rasToxyMatrix;
    vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);

    bool handlePicked = false;
    HandleInfoList handleInfoList = this->GetHandleInfoList();
    for (HandleInfo handleInfo : handleInfoList)
      {
      if (!handleInfo.IsVisible())
        {
        continue;
        }
      double* handleWorldPos = handleInfo.PositionWorld;
      rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
      handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected
      double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
      if (dist2 < maxPickingDistanceFromControlPoint2 && dist2 < closestDistance2)
        {
        closestDistance2 = dist2;
        foundComponentType = handleInfo.ComponentType;
        foundComponentIndex = handleInfo.Index;
        handlePicked = true;
        }
      }

    if (!handlePicked)
      {
      // Detect translation handle shaft
      for (HandleInfo handleInfo : handleInfoList)
        {
        if (!handleInfo.IsVisible() || handleInfo.ComponentType != InteractionTranslationHandle)
          {
          continue;
          }

        double* handleWorldPos = handleInfo.PositionWorld;
        rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
        handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected

        double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
        this->GetInteractionHandleOriginWorld(originWorldPos);
        double originDisplayPos[4] = { 0.0 };
        rasToxyMatrix->MultiplyPoint(originWorldPos, originDisplayPos);
        originDisplayPos[2] = displayPosition3[2]; // Handles are always projected

        double t = 0;
        double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
        double lineDistance2 = lineDistance * lineDistance;
        if (lineDistance2 < maxPickingDistanceFromControlPoint2 / 2.0 && lineDistance2 < closestDistance2)
          {
          closestDistance2 = lineDistance2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      }
    }
  else
    {

    bool handlePicked = false;
    HandleInfoList handleInfoList = this->GetHandleInfoList();
    for (HandleInfo handleInfo : handleInfoList)
      {
      if (!handleInfo.IsVisible())
        {
        continue;
        }

      double* handleWorldPos = handleInfo.PositionWorld;
      double handleDisplayPos[3] = { 0 };

      if (interactionEventData->IsDisplayPositionValid())
        {
        double pixelTolerance = this->InteractionSize / 2.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
          + this->PickingTolerance * this->ScreenScaleFactor;
        this->Renderer->SetWorldPoint(handleWorldPos);
        this->Renderer->WorldToDisplay();
        this->Renderer->GetDisplayPoint(handleDisplayPos);
        handleDisplayPos[2] = 0.0;
        double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
        if (dist2 < pixelTolerance * pixelTolerance && dist2 < closestDistance2)
          {
          closestDistance2 = dist2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          handlePicked = true;
          }
        }
      else
        {
        const double* worldPosition = interactionEventData->GetWorldPosition();
        double worldTolerance = this->InteractionSize / 2.0 +
          this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
        double dist2 = vtkMath::Distance2BetweenPoints(handleWorldPos, worldPosition);
        if (dist2 < worldTolerance * worldTolerance && dist2 < closestDistance2)
          {
          closestDistance2 = dist2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      }

    if (!handlePicked)
      {
      // Detect translation handle shaft
      for (HandleInfo handleInfo : handleInfoList)
        {
        if (!handleInfo.IsVisible() || handleInfo.ComponentType != InteractionTranslationHandle)
          {
          continue;
          }
        double* handleWorldPos = handleInfo.PositionWorld;
        double handleDisplayPos[3] = { 0 };

        if (interactionEventData->IsDisplayPositionValid())
          {
          double pixelTolerance = this->InteractionSize / 2.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
            + this->PickingTolerance * this->ScreenScaleFactor;
          this->Renderer->SetWorldPoint(handleWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(handleDisplayPos);
          handleDisplayPos[2] = 0.0;

          double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
          this->GetInteractionHandleOriginWorld(originWorldPos);
          double originDisplayPos[4] = { 0.0 };
          this->Renderer->SetWorldPoint(originWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(originDisplayPos);
          originDisplayPos[2] = displayPosition3[2]; // Handles are always projected
          double t = 0;
          double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
          double lineDistance2 = lineDistance * lineDistance;
          if (lineDistance < pixelTolerance && lineDistance2 < closestDistance2)
            {
            closestDistance2 = lineDistance2;
            foundComponentType = handleInfo.ComponentType;
            foundComponentIndex = handleInfo.Index;
            }
          }
        else
          {
          const double* worldPosition = interactionEventData->GetWorldPosition();
          double worldTolerance = this->InteractionSize / 2.0 +
            this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
          double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
          this->GetInteractionHandleOriginWorld(originWorldPos);
          double t;
          double lineDistance = vtkLine::DistanceToLine(worldPosition, originWorldPos, handleWorldPos, t);
          if (lineDistance < worldTolerance && lineDistance < closestDistance2)
            {
            closestDistance2 = lineDistance;
            foundComponentType = handleInfo.ComponentType;
            foundComponentIndex = handleInfo.Index;
            }
          }
        }
      }
    }

}


//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetViewScaleFactorAtPosition(double positionWorld[3])
{
  double viewScaleFactorMmPerPixel = 1.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return viewScaleFactorMmPerPixel;
    }

  vtkCamera * cam = this->Renderer->GetActiveCamera();
  if (cam->GetParallelProjection())
    {
    // Viewport: xmin, ymin, xmax, ymax; range: 0.0-1.0; origin is bottom left
    // Determine the available renderer size in pixels
    double minX = 0;
    double minY = 0;
    this->Renderer->NormalizedDisplayToDisplay(minX, minY);
    double maxX = 1;
    double maxY = 1;
    this->Renderer->NormalizedDisplayToDisplay(maxX, maxY);
    int rendererSizeInPixels[2] = { static_cast<int>(maxX - minX), static_cast<int>(maxY - minY) };
    // Parallel scale: height of the viewport in world-coordinate distances.
    // Larger numbers produce smaller images.
    viewScaleFactorMmPerPixel = (cam->GetParallelScale() * 2.0) / double(rendererSizeInPixels[1]);
    }
  else
    {
    double cameraFP[4] = { positionWorld[0], positionWorld[1], positionWorld[2], 1.0 };

    double cameraViewUp[3] = { 0 };
    cam->GetViewUp(cameraViewUp);
    vtkMath::Normalize(cameraViewUp);

    // Get distance in pixels between two points at unit distance above and below the focal point
    this->Renderer->SetWorldPoint(cameraFP[0] + cameraViewUp[0], cameraFP[1] + cameraViewUp[1], cameraFP[2] + cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double topCenter[3] = { 0 };
    this->Renderer->GetDisplayPoint(topCenter);
    topCenter[2] = 0.0;
    this->Renderer->SetWorldPoint(cameraFP[0] - cameraViewUp[0], cameraFP[1] - cameraViewUp[1], cameraFP[2] - cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double bottomCenter[3] = { 0 };
    this->Renderer->GetDisplayPoint(bottomCenter);
    bottomCenter[2] = 0.0;
    double distInPixels = sqrt(vtkMath::Distance2BetweenPoints(topCenter, bottomCenter));

    // if render window is not initialized yet then distInPixels == 0.0,
    // in that case just leave the default viewScaleFactorMmPerPixel
    if (distInPixels > 1e-3)
      {
      // 2.0 = 2x length of viewUp vector in mm (because viewUp is unit vector)
      viewScaleFactorMmPerPixel = 2.0 / distInPixels;
      }
    }
  return viewScaleFactorMmPerPixel;
}


//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetTransformationReferencePoint(double referencePointWorld[3])
{
  return true;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateFromMRML(
    vtkMRMLNode* vtkNotUsed(caller), unsigned long event, void *vtkNotUsed(callData))
{
  if (!this->Pipeline)
    {
    this->SetupInteractionPipeline();
    }

  this->NeedToRenderOn(); // TODO: to improve performance, call this only if it is actually needed

  if (this->GetSliceNode())
    {
    this->UpdatePlaneFromSliceNode();
    }

  if (this->Pipeline)
    {
    this->UpdateInteractionPipeline();
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateInteractionPipeline()
{
  if (!this->IsDisplayable())
    {
    this->Pipeline->Actor->SetVisibility(false);
    return;
    }

  this->UpdateViewScaleFactor();
  this->UpdateHandleSize();
  this->UpdateHandleColors();

  this->Pipeline->Actor->SetVisibility(true);
}

//----------------------------------------------------------------------
vtkPointPlacer* vtkMRMLInteractionWidgetRepresentation::GetPointPlacer()
{
  return this->PointPlacer;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetActors(vtkPropCollection* pc)
{
  if (this->Pipeline)
    {
    this->Pipeline->Actor->GetActors(pc);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::ReleaseGraphicsResources(vtkWindow* window)
{
  if (this->Pipeline)
    {
    this->Pipeline->Actor->ReleaseGraphicsResources(window);
    }
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOverlay(vtkViewport* viewport)
{
  int count = 0;
  if (this->Pipeline && this->Pipeline->Actor->GetVisibility())
    {
    count += this->Pipeline->Actor->RenderOverlay(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (this->Pipeline && this->Pipeline->Actor->GetVisibility())
    {
    this->UpdateHandleColors();
    this->UpdateViewScaleFactor();
    this->UpdateHandleSize();
    count += this->Pipeline->Actor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (this->Pipeline && this->Pipeline->Actor->GetVisibility())
    {
    this->Pipeline->Actor->SetPropertyKeys(this->GetPropertyKeys());
    count += this->Pipeline->Actor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
vtkTypeBool vtkMRMLInteractionWidgetRepresentation::HasTranslucentPolygonalGeometry()
{
  if (this->Pipeline && this->Pipeline->Actor->GetVisibility() &&
    this->Pipeline->Actor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::InteractionPipeline()
{
  /// Rotation pipeline
  this->RotationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->AxisRotationHandleSource = vtkSmartPointer<vtkSphereSource>::New();
  this->AxisRotationHandleSource->SetRadius(INTERACTION_HANDLE_RADIUS);
  this->AxisRotationHandleSource->SetPhiResolution(16);
  this->AxisRotationHandleSource->SetThetaResolution(16);

  this->AxisRotationArcSource = vtkSmartPointer<vtkArcSource>::New();
  this->AxisRotationArcSource->SetAngle(90);
  this->AxisRotationArcSource->SetCenter(-INTERACTION_HANDLE_ROTATION_ARC_RADIUS, 0, 0);
  this->AxisRotationArcSource->SetPoint1(
    INTERACTION_HANDLE_ROTATION_ARC_RADIUS / sqrt(2) - INTERACTION_HANDLE_ROTATION_ARC_RADIUS,
    -INTERACTION_HANDLE_ROTATION_ARC_RADIUS / sqrt(2), 0);
  this->AxisRotationArcSource->SetPoint2(
    INTERACTION_HANDLE_ROTATION_ARC_RADIUS / sqrt(2) - INTERACTION_HANDLE_ROTATION_ARC_RADIUS,
    INTERACTION_HANDLE_ROTATION_ARC_RADIUS / sqrt(2), 0);
  this->AxisRotationArcSource->SetResolution(16);

  this->AxisRotationTubeFilter = vtkSmartPointer<vtkTubeFilter>::New();
  this->AxisRotationTubeFilter->SetInputConnection(this->AxisRotationArcSource->GetOutputPort());
  this->AxisRotationTubeFilter->SetRadius(INTERACTION_HANDLE_ROTATION_ARC_TUBE_RADIUS);
  this->AxisRotationTubeFilter->SetNumberOfSides(16);
  this->AxisRotationTubeFilter->SetCapping(true);

  this->RotationScaleTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->RotationScaleTransform->SetInputData(this->RotationHandlePoints);
  this->RotationScaleTransform->SetTransform(vtkNew<vtkTransform>());

  this->AxisRotationGlyphSource = vtkSmartPointer <vtkAppendPolyData>::New();
  this->AxisRotationGlyphSource->AddInputConnection(this->AxisRotationHandleSource->GetOutputPort());
  this->AxisRotationGlyphSource->AddInputConnection(this->AxisRotationTubeFilter->GetOutputPort());
  this->AxisRotationGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisRotationGlypher->SetInputConnection(this->RotationScaleTransform->GetOutputPort());
  this->AxisRotationGlypher->SetSourceConnection(this->AxisRotationGlyphSource->GetOutputPort());
  this->AxisRotationGlypher->ScalingOff();
  this->AxisRotationGlypher->ExtractEigenvaluesOff();
  this->AxisRotationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  this->AxisTranslationGlyphSource = vtkSmartPointer<vtkArrowSource>::New();
  this->AxisTranslationGlyphSource->SetTipRadius(INTERACTION_TRANSLATION_HANDLE_RADIUS);
  this->AxisTranslationGlyphSource->SetTipLength(INTERACTION_TRANSLATION_HANDLE_DIAMETER);
  this->AxisTranslationGlyphSource->SetShaftRadius(INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS);
  this->AxisTranslationGlyphSource->SetTipResolution(16);
  this->AxisTranslationGlyphSource->SetShaftResolution(16);
  this->AxisTranslationGlyphSource->InvertOn();

  vtkNew<vtkTransform> translationGlyphTransformer;
  translationGlyphTransformer->Translate(INTERACTION_HANDLE_RADIUS, 0, 0);
  translationGlyphTransformer->RotateY(180);

  this->AxisTranslationGlyphTransformer = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->AxisTranslationGlyphTransformer->SetTransform(translationGlyphTransformer);
  this->AxisTranslationGlyphTransformer->SetInputConnection(this->AxisTranslationGlyphSource->GetOutputPort());

  /// Translation pipeline
  this->TranslationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->TranslationScaleTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->TranslationScaleTransform->SetInputData(this->TranslationHandlePoints);
  this->TranslationScaleTransform->SetTransform(vtkNew<vtkTransform>());

  this->AxisTranslationGlypher = vtkSmartPointer<vtkGlyph3D>::New();
  this->AxisTranslationGlypher->SetInputConnection(this->TranslationScaleTransform->GetOutputPort());
  this->AxisTranslationGlypher->SetSourceConnection(0, this->AxisTranslationGlyphTransformer->GetOutputPort());
  this->AxisTranslationGlypher->SetSourceConnection(1, this->AxisRotationHandleSource->GetOutputPort());
  this->AxisTranslationGlypher->ScalingOn();
  this->AxisTranslationGlypher->SetScaleModeToDataScalingOff();
  this->AxisTranslationGlypher->SetIndexModeToScalar();
  this->AxisTranslationGlypher->SetColorModeToColorByScalar();
  this->AxisTranslationGlypher->OrientOn();
  this->AxisTranslationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "glyphIndex"); // Glyph shape
  this->AxisTranslationGlypher->SetInputArrayToProcess(1, 0, 0, 0, "orientation"); // Orientation direction array

  this->AxisScaleHandleSource = vtkSmartPointer<vtkSphereSource>::New();
  this->AxisScaleHandleSource->SetRadius(INTERACTION_HANDLE_RADIUS);
  this->AxisScaleHandleSource->SetPhiResolution(16);
  this->AxisScaleHandleSource->SetThetaResolution(16);

  /// Scale pipeline
  this->ScaleHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->ScaleScaleTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ScaleScaleTransform->SetInputData(this->ScaleHandlePoints);
  this->ScaleScaleTransform->SetTransform(vtkNew<vtkTransform>());

  this->AxisScaleGlypher = vtkSmartPointer<vtkGlyph3D>::New();
  this->AxisScaleGlypher->SetInputConnection(this->ScaleScaleTransform->GetOutputPort());
  this->AxisScaleGlypher->SetSourceConnection(this->AxisRotationHandleSource->GetOutputPort());
  this->AxisScaleGlypher->ScalingOn();
  this->AxisScaleGlypher->SetScaleModeToDataScalingOff();
  this->AxisScaleGlypher->SetIndexModeToScalar();
  this->AxisScaleGlypher->SetColorModeToColorByScalar();

  this->Append = vtkSmartPointer<vtkAppendPolyData>::New();
  this->Append->AddInputConnection(this->AxisTranslationGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisRotationGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisScaleGlypher->GetOutputPort());

  this->HandleToWorldTransform = vtkSmartPointer<vtkTransform>::New();
  this->HandleToWorldTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->HandleToWorldTransformFilter->SetInputConnection(this->Append->GetOutputPort());
  this->HandleToWorldTransformFilter->SetTransform(this->HandleToWorldTransform);

  this->ColorTable = vtkSmartPointer<vtkLookupTable>::New();

  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();

  this->Mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->Mapper->SetInputConnection(this->HandleToWorldTransformFilter->GetOutputPort());
  this->Mapper->SetColorModeToMapScalars();
  this->Mapper->ColorByArrayComponent("colorIndex", 0);
  this->Mapper->SetLookupTable(this->ColorTable);
  this->Mapper->ScalarVisibilityOn();
  this->Mapper->UseLookupTableScalarRangeOn();
  this->Mapper->SetTransformCoordinate(coordinate);

  this->Property = vtkSmartPointer<vtkProperty2D>::New();
  this->Property->SetPointSize(0.0);
  this->Property->SetLineWidth(0.0);

  this->Actor = vtkSmartPointer<vtkActor2D>::New();
  this->Actor->SetProperty(this->Property);
  this->Actor->SetMapper(this->Mapper);

  this->WorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->WorldToSliceTransformFilter->SetTransform(vtkNew<vtkTransform>());
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::~InteractionPipeline() = default;

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::InitializePipeline()
{
  this->CreateRotationHandles();
  this->CreateTranslationHandles();
  this->CreateScaleHandles();
  this->UpdateHandleColors();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateRotationHandles()
{
  vtkNew<vtkPoints> points;

  double xRotationHandle[3] = { 0, 1, 1 }; // X-axis
  vtkMath::Normalize(xRotationHandle);
  points->InsertNextPoint(xRotationHandle);
  double yRotationHandle[3] = { 1, 0, 1 }; // Y-axis
  vtkMath::Normalize(yRotationHandle);
  points->InsertNextPoint(yRotationHandle);
  double zRotationHandle[3] = { 1, 1, 0 }; // Z-axis
  vtkMath::Normalize(zRotationHandle);
  points->InsertNextPoint(zRotationHandle);
  this->Pipeline->RotationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  vtkNew<vtkTransform> xRotationOrientation;
  xRotationOrientation->RotateX(90);
  xRotationOrientation->RotateY(90);
  xRotationOrientation->RotateZ(45);
  vtkMatrix4x4* xRotationMatrix = xRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(xRotationMatrix->GetElement(0, 0), xRotationMatrix->GetElement(1, 0), xRotationMatrix->GetElement(2, 0),
                                     xRotationMatrix->GetElement(0, 1), xRotationMatrix->GetElement(1, 1), xRotationMatrix->GetElement(2, 1),
                                     xRotationMatrix->GetElement(0, 2), xRotationMatrix->GetElement(1, 2), xRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> yRotationOrientation;
  yRotationOrientation->RotateX(90);
  yRotationOrientation->RotateZ(45);
  vtkMatrix4x4* yRotationMatrix = yRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(yRotationMatrix->GetElement(0, 0), yRotationMatrix->GetElement(1, 0), yRotationMatrix->GetElement(2, 0),
                                     yRotationMatrix->GetElement(0, 1), yRotationMatrix->GetElement(1, 1), yRotationMatrix->GetElement(2, 1),
                                     yRotationMatrix->GetElement(0, 2), yRotationMatrix->GetElement(1, 2), yRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> zRotationOrientation;
  zRotationOrientation->RotateZ(45);
  vtkMatrix4x4* zRotationMatrix = zRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(zRotationMatrix->GetElement(0, 0), zRotationMatrix->GetElement(1, 0), zRotationMatrix->GetElement(2, 0),
                                     zRotationMatrix->GetElement(0, 1), zRotationMatrix->GetElement(1, 1), zRotationMatrix->GetElement(2, 1),
                                     zRotationMatrix->GetElement(0, 2), zRotationMatrix->GetElement(1, 2), zRotationMatrix->GetElement(2, 2));
  this->Pipeline->RotationHandlePoints->GetPointData()->AddArray(orientationArray);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateTranslationHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1, 0, 0); // X-axis
  points->InsertNextPoint(0, 1, 0); // Y-axis
  points->InsertNextPoint(0, 0, 1); // Z-axis
  points->InsertNextPoint(0, 0, 0); // Free translation
  this->Pipeline->TranslationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(3);
  orientationArray->InsertNextTuple3(1, 0, 0);
  orientationArray->InsertNextTuple3(0, 1, 0);
  orientationArray->InsertNextTuple3(0, 0, 1);
  orientationArray->InsertNextTuple3(1, 0, 0); // Free translation
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkDoubleArray> glyphIndexArray;
  glyphIndexArray->SetName("glyphIndex");
  glyphIndexArray->SetNumberOfComponents(1);
  glyphIndexArray->InsertNextTuple1(0);
  glyphIndexArray->InsertNextTuple1(0);
  glyphIndexArray->InsertNextTuple1(0);
  glyphIndexArray->InsertNextTuple1(1);
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(glyphIndexArray);

}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateScaleHandles()
{
  vtkNew<vtkPoints> points; // Currently not enabled by default
  //points->InsertNextPoint(1.5, 0.0, 0.0); // X-axis
  //points->InsertNextPoint(0.0, 1.5, 0.0); // Y-axis
  //points->InsertNextPoint(0.0, 0.0, 1.5); // Z-axis
  this->Pipeline->ScaleHandlePoints->SetPoints(points);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->ScaleHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleColors()
{
  int numberOfHandles = this->Pipeline->RotationHandlePoints->GetNumberOfPoints()
    + this->Pipeline->TranslationHandlePoints->GetNumberOfPoints()
    + this->Pipeline->ScaleHandlePoints->GetNumberOfPoints();
  this->Pipeline->ColorTable->SetNumberOfTableValues(numberOfHandles);
  this->Pipeline->ColorTable->SetTableRange(0, double(numberOfHandles) - 1);

  int colorIndex = 0;
  double color[4] = { 0.0, 0.0, 0.0, 0.0 };

  // Rotation handles
  vtkSmartPointer<vtkFloatArray> rotationColorArray = vtkFloatArray::SafeDownCast(
    this->Pipeline->RotationHandlePoints->GetPointData()->GetAbstractArray("colorIndex"));
  if (!rotationColorArray)
    {
    rotationColorArray = vtkSmartPointer<vtkFloatArray>::New();
    rotationColorArray->SetName("colorIndex");
    rotationColorArray->SetNumberOfComponents(1);
    this->Pipeline->RotationHandlePoints->GetPointData()->AddArray(rotationColorArray);
    this->Pipeline->RotationHandlePoints->GetPointData()->SetActiveScalars("colorIndex");
    }
  rotationColorArray->Initialize();
  rotationColorArray->SetNumberOfTuples(this->Pipeline->RotationHandlePoints->GetNumberOfPoints());
  for (int i = 0; i < this->Pipeline->RotationHandlePoints->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(InteractionRotationHandle, i, color);
    this->Pipeline->ColorTable->SetTableValue(colorIndex, color);
    rotationColorArray->SetTuple1(i, colorIndex);
    ++colorIndex;
    }

  // Translation handles
  vtkSmartPointer<vtkFloatArray> translationColorArray = vtkFloatArray::SafeDownCast(
    this->Pipeline->TranslationHandlePoints->GetPointData()->GetAbstractArray("colorIndex"));
  if (!translationColorArray)
    {
    translationColorArray = vtkSmartPointer<vtkFloatArray>::New();
    translationColorArray->SetName("colorIndex");
    translationColorArray->SetNumberOfComponents(1);
    this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(translationColorArray);
    this->Pipeline->TranslationHandlePoints->GetPointData()->SetActiveScalars("colorIndex");
    }
  translationColorArray->Initialize();
  translationColorArray->SetNumberOfTuples(this->Pipeline->TranslationHandlePoints->GetNumberOfPoints());
  for (int i = 0; i < this->Pipeline->TranslationHandlePoints->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(InteractionTranslationHandle, i, color);
    this->Pipeline->ColorTable->SetTableValue(colorIndex, color);
    translationColorArray->SetTuple1(i, colorIndex);
    ++colorIndex;
    }

  // Rotation handles
  vtkSmartPointer<vtkFloatArray> scaleColorArray = vtkFloatArray::SafeDownCast(
    this->Pipeline->ScaleHandlePoints->GetPointData()->GetAbstractArray("colorIndex"));
  if (!scaleColorArray)
    {
    scaleColorArray = vtkSmartPointer<vtkFloatArray>::New();
    scaleColorArray->SetName("colorIndex");
    scaleColorArray->SetNumberOfComponents(1);
    this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(scaleColorArray);
    this->Pipeline->ScaleHandlePoints->GetPointData()->SetActiveScalars("colorIndex");
    }
  scaleColorArray->Initialize();
  scaleColorArray->SetNumberOfTuples(this->Pipeline->ScaleHandlePoints->GetNumberOfPoints());
  for (int i = 0; i < this->Pipeline->ScaleHandlePoints->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(InteractionScaleHandle, i, color);
    this->Pipeline->ColorTable->SetTableValue(colorIndex, color);
    scaleColorArray->SetTuple1(i, colorIndex);
    ++colorIndex;
    }

  this->Pipeline->ColorTable->Build();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
    {
    return;
    }

  double red[4]    = { 1.00, 0.00, 0.00, 1.00 };
  double green[4]  = { 0.00, 1.00, 0.00, 1.00 };
  double blue[4]   = { 0.00, 0.00, 1.00, 1.00 };
  double orange[4] = { 1.00, 0.50, 0.00, 1.00 };
  double white[4]  = { 1.00, 1.00, 1.00, 1.00 };
  double yellow[4] = { 1.00, 1.00, 0.00, 1.00 };

  double* currentColor = red;
  switch (index)
    {
    case 0:
      currentColor = red;
      break;
    case 1:
      currentColor = green;
      break;
    case 2:
      currentColor = blue;
      break;
    case 3:
      currentColor = orange;
      break;
    default:
      currentColor = white;
      break;
    }

  double opacity = this->GetHandleOpacity(type, index);
  if (this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index)
    {
    currentColor = yellow;
    opacity = 1.0;
    }

  for (int i = 0; i < 3; ++i)
    {
    color[i] = currentColor[i];
    }

  vtkPolyData* handlePoints = nullptr;
  if (type == InteractionTranslationHandle)
    {
    handlePoints = this->Pipeline->TranslationHandlePoints;
    }
  else if (type == InteractionRotationHandle)
    {
    handlePoints = this->Pipeline->RotationHandlePoints;
    }
  else if (type == InteractionScaleHandle)
    {
    handlePoints = this->Pipeline->ScaleHandlePoints;
    }

  vtkIdTypeArray* visibilityArray = nullptr;
  if (handlePoints)
    {
    visibilityArray = vtkIdTypeArray::SafeDownCast(handlePoints->GetPointData()->GetArray("visibility"));
    }

  if (visibilityArray)
    {
    opacity = visibilityArray->GetValue(index) ? opacity : 0.0;
    }
  color[3] = opacity;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetHandleOpacity(int type, int index)
{
  // Determine if the handle should be displayed
  bool handleVisible = true;

  if (false/*TODO. Add function to toggle handle display visibility*/)
    {
    //TODO: Add handle visiblity toggle
    /*handleVisible = displayNode->GetHandleVisibility(type);*/
    }
  if (!handleVisible)
    {
    return 0.0;
    }

  double opacity = 1.0;
  if (type == InteractionTranslationHandle && index == 3)
    {
    // Free transform handle is always visible regardless of angle
    return opacity;
    }

  double viewNormal[3] = { 0.0, 0.0, 0.0 };
  this->GetViewPlaneNormal(viewNormal);

  double axis[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisWorld(type, index, axis);
  if (vtkMath::Dot(viewNormal, axis) < 0)
    {
    vtkMath::MultiplyScalar(axis, -1);
    }

  double fadeAngleRange = this->StartFadeAngle - this->EndFadeAngle;
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal, axis));
  if (type == InteractionRotationHandle)
    {
    // Fade happens when the axis approaches 90 degrees from the view normal
    if (angle > 90 - this->EndFadeAngle)
      {
      opacity = 0.0;
      }
    else if (angle > 90 - this->StartFadeAngle)
      {
      double difference = angle - (90 - this->StartFadeAngle);
      opacity = 1.0 - (difference / fadeAngleRange);
      }
    }
  else if (type == InteractionTranslationHandle || type == InteractionScaleHandle)
    {
    // Fade happens when the axis approaches 0 degrees from the view normal
    if (angle < this->EndFadeAngle)
      {
      opacity = 0.0;
      }
    else if (angle < this->StartFadeAngle)
      {
      double difference = angle - this->EndFadeAngle;
      opacity = (difference / fadeAngleRange);
      }
    }
  return opacity;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetViewPlaneNormal(double normal[3])
{
  if (!normal)
    {
    return;
    }

  if (this->GetSliceNode())
    {
    double viewPlaneNormal4[4] = { 0, 0, 1, 0 };
    vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->GetViewNode());
    if (sliceNode)
      {
      sliceNode->GetSliceToRAS()->MultiplyPoint(viewPlaneNormal4, viewPlaneNormal4);
      }
    normal[0] = viewPlaneNormal4[0];
    normal[1] = viewPlaneNormal4[1];
    normal[2] = viewPlaneNormal4[2];
    }
  else if (this->GetRenderer() && this->GetRenderer()->GetActiveCamera())
    {
    vtkCamera* camera = this->GetRenderer()->GetActiveCamera();
    camera->GetViewPlaneNormal(normal);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetWidgetScale(double scale)
{
  vtkNew<vtkTransform> scaleTransform;
  scaleTransform->Scale(scale, scale, scale);
  this->Pipeline->RotationScaleTransform->SetTransform(scaleTransform);
  this->Pipeline->TranslationScaleTransform->SetTransform(scaleTransform);
  this->Pipeline->ScaleScaleTransform->SetTransform(scaleTransform);
  this->Pipeline->AxisRotationGlypher->SetScaleFactor(scale);
  this->Pipeline->AxisTranslationGlypher->SetScaleFactor(scale);
  this->Pipeline->AxisScaleGlypher->SetScaleFactor(scale);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleOriginWorld(double originWorld[3])
{
  if (!originWorld)
    {
    return;
    }

  double handleOrigin[3] = { 0,0,0 };
  this->Pipeline->HandleToWorldTransform->TransformPoint(handleOrigin, originWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisWorld(int type, int index, double axisWorld[3])
{
  if (!axisWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument!");
    return;
    }

  axisWorld[0] = 0.0;
  axisWorld[1] = 0.0;
  axisWorld[2] = 0.0;

  if (type == InteractionTranslationHandle)
    {
    switch (index)
      {
      case 0:
        axisWorld[0] = 1.0;
        break;
      case 1:
        axisWorld[1] = 1.0;
        break;
      case 2:
        axisWorld[2] = 1.0;
        break;
      default:
        break;
      }
    }
  else if (type == InteractionRotationHandle)
    {
    switch (index)
      {
    case 0:
      axisWorld[0] = 1.0;
      break;
    case 1:
      axisWorld[1] = 1.0;
      break;
    case 2:
      axisWorld[2] = 1.0;
        break;
      default:
        break;
      }
    }
  else if (type == InteractionScaleHandle)
    {
    switch (index)
      {
      case 0:
        axisWorld[0] = 1.0;
        break;
      case 1:
        axisWorld[1] = 1.0;
        break;
      case 2:
        axisWorld[2] = 1.0;
        break;
      default:
        break;
      }
    }
  double origin[3] = { 0.0, 0.0, 0.0 };
  this->Pipeline->HandleToWorldTransform->TransformVectorAtPoint(origin, axisWorld, axisWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandlePositionWorld(int type, int index, double positionWorld[3])
{
  if (!positionWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandlePositionWorld: Invalid position argument!");
    }

  if (type == InteractionRotationHandle)
    {
    this->Pipeline->RotationHandlePoints->GetPoint(index, positionWorld);
    this->Pipeline->RotationScaleTransform->GetTransform()->TransformPoint(positionWorld, positionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
    }
  else if (type == InteractionTranslationHandle)
    {
    this->Pipeline->TranslationHandlePoints->GetPoint(index, positionWorld);
    this->Pipeline->TranslationScaleTransform->GetTransform()->TransformPoint(positionWorld, positionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
    }
  else if (type == InteractionScaleHandle)
    {
    this->Pipeline->ScaleHandlePoints->GetPoint(index, positionWorld);
    this->Pipeline->ScaleScaleTransform->GetTransform()->TransformPoint(positionWorld, positionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
    }
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::HandleInfoList vtkMRMLInteractionWidgetRepresentation::GetHandleInfoList()
{
  HandleInfoList handleInfoList;
  for (int i = 0; i < this->Pipeline->RotationHandlePoints->GetNumberOfPoints(); ++i)
    {
    double handlePositionLocal[3] = { 0 };
    double handlePositionWorld[3] = { 0 };
    this->Pipeline->RotationHandlePoints->GetPoint(i, handlePositionLocal);
    this->Pipeline->RotationScaleTransform->GetTransform()->TransformPoint(handlePositionLocal, handlePositionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(handlePositionWorld, handlePositionWorld);
    double color[4] = { 0 };
    this->GetHandleColor(InteractionRotationHandle, i, color);
    HandleInfo info(i, InteractionRotationHandle, handlePositionWorld, handlePositionLocal, color);
    handleInfoList.push_back(info);
    }

  for (int i = 0; i < this->Pipeline->TranslationHandlePoints->GetNumberOfPoints(); ++i)
    {
    double handlePositionLocal[3] = { 0 };
    double handlePositionWorld[3] = { 0 };
    this->Pipeline->TranslationHandlePoints->GetPoint(i, handlePositionLocal);
    this->Pipeline->TranslationScaleTransform->GetTransform()->TransformPoint(handlePositionLocal, handlePositionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(handlePositionWorld, handlePositionWorld);
    double color[4] = { 0 };
    this->GetHandleColor(InteractionTranslationHandle, i, color);
    HandleInfo info(i, InteractionTranslationHandle, handlePositionWorld, handlePositionLocal, color);
    handleInfoList.push_back(info);
    }

  for (int i = 0; i < this->Pipeline->ScaleHandlePoints->GetNumberOfPoints(); ++i)
    {
    double handlePositionLocal[3] = { 0 };
    double handlePositionWorld[3] = { 0 };
    this->Pipeline->ScaleHandlePoints->GetPoint(i, handlePositionLocal);
    this->Pipeline->ScaleScaleTransform->GetTransform()->TransformPoint(handlePositionLocal, handlePositionWorld);
    this->Pipeline->HandleToWorldTransform->TransformPoint(handlePositionWorld, handlePositionWorld);
    double color[4] = { 0 };
    this->GetHandleColor(InteractionScaleHandle, i, color);
    HandleInfo info(i, InteractionScaleHandle, handlePositionWorld, handlePositionLocal, color);
    handleInfoList.push_back(info);
    }

  return handleInfoList;
}

//----------------------------------------------------------------------
vtkMRMLSliceNode* vtkMRMLInteractionWidgetRepresentation::GetSliceNode()
{
  return vtkMRMLSliceNode::SafeDownCast(this->ViewNode);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetSliceToWorldCoordinates(const double slicePos[2],
  double worldPos[3])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!this->Renderer || !sliceNode)
    {
    return;
    }

  double xyzw[4] =
    {
    slicePos[0] - this->Renderer->GetOrigin()[0],
    slicePos[1] - this->Renderer->GetOrigin()[1],
    0.0,
    1.0
    };
  double rasw[4] = { 0.0, 0.0, 0.0, 1.0 };

  this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(xyzw, rasw);

  worldPos[0] = rasw[0] / rasw[3];
  worldPos[1] = rasw[1] / rasw[3];
  worldPos[2] = rasw[2] / rasw[3];
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdatePlaneFromSliceNode()
{
  if (!this->GetSliceNode())
    {
    return;
    }

  vtkMatrix4x4* sliceXYToRAS = this->GetSliceNode()->GetXYToRAS();

  // Update transformation to slice
  vtkNew<vtkMatrix4x4> rasToSliceXY;
  vtkMatrix4x4::Invert(sliceXYToRAS, rasToSliceXY.GetPointer());
  // Project all points to the slice plane (slice Z coordinate = 0)
  rasToSliceXY->SetElement(2, 0, 0);
  rasToSliceXY->SetElement(2, 1, 0);
  rasToSliceXY->SetElement(2, 2, 0);
  this->WorldToSliceTransform->SetMatrix(rasToSliceXY.GetPointer());

  // Update slice plane (for distance computation)
  double normal[3];
  double origin[3];
  const double planeOrientation = 1.0; // +/-1: orientation of the normal
  for (int i = 0; i < 3; i++)
  {
    normal[i] = planeOrientation * sliceXYToRAS->GetElement(i, 2);
    origin[i] = sliceXYToRAS->GetElement(i, 3);
  }
  vtkMath::Normalize(normal);
  this->SlicePlane->SetNormal(normal);
  this->SlicePlane->SetOrigin(origin);
  this->SlicePlane->Modified();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateViewScaleFactor()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return;
    }

  int* screenSize = this->Renderer->GetRenderWindow()->GetScreenSize();
  double screenSizePixel = sqrt(screenSize[0] * screenSize[0] + screenSize[1] * screenSize[1]);
  if (screenSizePixel < 1.0)
    {
    // render window is not fully initialized yet
    return;
    }
  this->ScreenSizePixel = screenSizePixel;

  if (this->GetSliceNode())
    {
    vtkMatrix4x4* xyToSlice = this->GetSliceNode()->GetXYToSlice();
    this->ViewScaleFactorMmPerPixel = sqrt(xyToSlice->GetElement(0, 1) * xyToSlice->GetElement(0, 1)
      + xyToSlice->GetElement(1, 1) * xyToSlice->GetElement(1, 1));
    }
  else
    {
    double cameraFP[3] = { 0.0 };
    this->Renderer->GetActiveCamera()->GetFocalPoint(cameraFP);
    this->ViewScaleFactorMmPerPixel = this->GetViewScaleFactorAtPosition(cameraFP);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleSize()
{
  if (!this->GetInteractionSizeAbsolute())
    {
    this->InteractionSize = this->ScreenSizePixel * this->ScreenScaleFactor
      * this->GetInteractionScale() / 100.0 * this->ViewScaleFactorMmPerPixel;
    }
  else
    {
    this->InteractionSize = this->GetInteractionSize() / this->ViewScaleFactorMmPerPixel;
    }
  this->SetWidgetScale(this->InteractionSize);
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionScale()
{
  return 3.0;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionSize()
{
  return 1.0;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return false;
}
