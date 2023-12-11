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
  Care Ontario, OpenAnatomy, and Brigham and Women�s Hospital through NIH grant R01MH112748.

==============================================================================*/

// VTK includes
#include <vtkAppendPolyData.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkEllipseArcSource.h>
#include <vtkFloatArray.h>
#include <vtkFocalPlanePointPlacer.h>
#include <vtkGlyph3D.h>
#include <vtkLine.h>
#include <vtkLineSource.h>
#include <vtkLookupTable.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLViewNode.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointSetToLabelHierarchy.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSphereSource.h>
#include <vtkStringArray.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTensorGlyph.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkTubeFilter.h>

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>

#include <vtkMRMLInteractionWidgetRepresentation.h>

//----------------------------------------------------------------------
static const double INTERACTION_HANDLE_RADIUS = 0.0625;
static const double INTERACTION_HANDLE_DIAMETER = INTERACTION_HANDLE_RADIUS * 2.0;
static const double INTERACTION_HANDLE_ROTATION_ARC_TUBE_RADIUS = INTERACTION_HANDLE_RADIUS * 0.4;

static const double INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS = 1.2;
static const double INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS = 1.1;
static const double INTERACTION_HANDLE_ROTATION_ARC_DEGREES = 45.0;
static const int INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION = 45;

static const double INTERACTION_TRANSLATION_HANDLE_LENGTH= 0.75;
static const double INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS = 0.10;
static const double INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS = 0.05;

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
  this->NeedToRenderOn();
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
  if (!viewNode || !this->IsDisplayable() || !interactionEventData)
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
        double originDisplayPos[4] = { 0.0, 0.0, 0.0, 0.0 };
        rasToxyMatrix->MultiplyPoint(originWorldPos, originDisplayPos);
        originDisplayPos[2] = displayPosition3[2]; // Handles are always projected

        double t = 0.0; // Not used
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
      double handleDisplayPos[3] = { 0.0, 0.0, 0.0 };

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
        double handleDisplayPos[3] = { 0.0, 0.0, 0.0 };

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
          double originDisplayPos[4] = { 0.0, 0.0, 0.0, 0.0 };
          this->Renderer->SetWorldPoint(originWorldPos);
          this->Renderer->WorldToDisplay();
          this->Renderer->GetDisplayPoint(originDisplayPos);
          originDisplayPos[2] = displayPosition3[2]; // Handles are always projected
          double t = 0.0; // Not used
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
          double t = 0.0; // Not used
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

    double cameraViewUp[3] = { 0.0, 0.0, 0.0 };
    cam->GetViewUp(cameraViewUp);
    vtkMath::Normalize(cameraViewUp);

    // Get distance in pixels between two points at unit distance above and below the focal point
    this->Renderer->SetWorldPoint(cameraFP[0] + cameraViewUp[0], cameraFP[1] + cameraViewUp[1], cameraFP[2] + cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double topCenter[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetDisplayPoint(topCenter);
    topCenter[2] = 0.0;
    this->Renderer->SetWorldPoint(cameraFP[0] - cameraViewUp[0], cameraFP[1] - cameraViewUp[1], cameraFP[2] - cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double bottomCenter[3] = { 0.0, 0.0, 0.0 };
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
  double origin[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToWorldTransform()->TransformPoint(origin, referencePointWorld);
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

  if (this->GetSliceNode())
    {
    this->UpdateSlicePlaneFromSliceNode();
    }

  if (this->Pipeline)
    {
    this->UpdateInteractionPipeline();
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateInteractionPipeline()
{
  bool currentVisibility = this->GetVisibility();
  if (!this->IsDisplayable())
    {
    this->SetVisibility(false);
    if (currentVisibility)
      {
      // If we are changing visibility, we need to render.
      this->NeedToRenderOn();
      }
    return;
    }

  bool needToRender = false;
  double currentViewScaleFactorMmPerPixel = this->ViewScaleFactorMmPerPixel;
  double currentScreenSizePixel = this->ScreenSizePixel;
  this->UpdateViewScaleFactor();
  if (currentViewScaleFactorMmPerPixel != this->ViewScaleFactorMmPerPixel ||
    currentScreenSizePixel != this->ScreenSizePixel)
    {
    needToRender = true;
    }

  double currentInteractionSize = this->InteractionSize;
  this->UpdateHandleSize();
  if (currentInteractionSize != this->InteractionSize)
    {
    needToRender = true;
    }

  // TODO: need to set the render flag
  needToRender = true;
  this->UpdateHandleColors();

  if (!currentVisibility)
    {
    // If we are changing visibility, we need to render.
    needToRender = true;
    }
  this->SetVisibility(true);

  this->UpdateHandleToWorldTransform();

  if (needToRender)
    {
    this->NeedToRenderOn();
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleToWorldTransform()
{
  vtkTransform* handleToWorldTransform = this->GetHandleToWorldTransform();
  this->UpdateHandleToWorldTransform(handleToWorldTransform);
  this->OrthoganalizeTransform(handleToWorldTransform);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::OrthoganalizeTransform(vtkTransform* transform)
{
  double x[3] = { 1.0, 0.0, 0.0 };
  double y[3] = { 0.0, 1.0, 0.0 };
  double z[3] = { 0.0, 0.0, 1.0 };

  transform->TransformVector(x, x);
  transform->TransformVector(y, y);
  transform->TransformVector(z, z);
  vtkMath::Normalize(x);
  vtkMath::Normalize(y);
  vtkMath::Normalize(z);

  double xOrthogonal[3] = { 1.0, 0.0, 0.0 };
  double yOrthogonal[3] = { 0.0, 1.0, 0.0 };
  double zOrthogonal[3] = { z[0], z[1], z[2] };
  vtkMath::Cross(zOrthogonal, x, yOrthogonal);
  vtkMath::Normalize(yOrthogonal);
  vtkMath::Cross(yOrthogonal, zOrthogonal, xOrthogonal);
  vtkMath::Normalize(xOrthogonal);

  vtkNew<vtkMatrix4x4> orthogonalMatrix;
  orthogonalMatrix->DeepCopy(transform->GetMatrix());
  for (int i = 0; i < 3; ++i)
    {
    orthogonalMatrix->SetElement(i, 0, xOrthogonal[i]);
    orthogonalMatrix->SetElement(i, 1, yOrthogonal[i]);
    orthogonalMatrix->SetElement(i, 2, zOrthogonal[i]);
    }

  transform->Identity();
  transform->Concatenate(orthogonalMatrix);
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
    this->UpdateTranslationHandleOrientation();
    this->UpdateScaleHandleOrientation();
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

  this->RotationScaleTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->RotationScaleTransformFilter->SetInputData(this->RotationHandlePoints);
  this->RotationScaleTransformFilter->SetTransform(vtkNew<vtkTransform>());

  vtkNew<vtkPoints> rotationPts;
  vtkNew<vtkIdList> rotationPoly;
  vtkNew<vtkIdList> rotationLine;

  vtkNew<vtkEllipseArcSource> outerArcSource;
  outerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS, 0.0, 0.0);
  outerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  outerArcSource->SetCenter(-1.0, 0, 0);
  outerArcSource->SetNormal(0, 0, 1);
  outerArcSource->SetRatio(1.0);
  outerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  outerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  outerArcSource->Update();
  for (int i = 0; i < outerArcSource->GetOutput()->GetNumberOfPoints(); ++i)
    {
    double point[3];
    outerArcSource->GetOutput()->GetPoint(i, point);
    vtkIdType id = rotationPts->InsertNextPoint(point);
    rotationPoly->InsertNextId(id);
    rotationLine->InsertNextId(id);
    }

  vtkNew<vtkEllipseArcSource> innerArcSource;
  innerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS, 0.0, 0.0);
  innerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  innerArcSource->SetCenter(-1.0, 0, 0);
  innerArcSource->SetNormal(0, 0, 1);
  innerArcSource->SetRatio(1.0);
  innerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  innerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  innerArcSource->Update();
  for (int i = innerArcSource->GetOutput()->GetNumberOfPoints() - 1; i >= 0; --i)
    {
    double point[3];
    innerArcSource->GetOutput()->GetPoint(i, point);
    vtkIdType id = rotationPts->InsertNextPoint(point);
    rotationPoly->InsertNextId(id);
    rotationLine->InsertNextId(id);
    }

  rotationLine->InsertNextId(0);


  this->AxisRotationPolyData = vtkSmartPointer <vtkPolyData>::New();
  this->AxisRotationPolyData->SetPoints(rotationPts);
  this->AxisRotationPolyData->SetPolys(vtkNew<vtkCellArray>());
  this->AxisRotationPolyData->InsertNextCell(VTK_POLYGON, rotationPoly);

  vtkNew<vtkTriangleFilter> triangleFilter;
  triangleFilter->SetInputData(this->AxisRotationPolyData);

  this->AxisRotationGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisRotationGlypher->SetInputConnection(this->RotationScaleTransformFilter->GetOutputPort());
  this->AxisRotationGlypher->SetSourceConnection(triangleFilter->GetOutputPort());
  this->AxisRotationGlypher->ScalingOff();
  this->AxisRotationGlypher->ExtractEigenvaluesOff();
  this->AxisRotationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  this->AxisRotationOutlinePolyData = vtkSmartPointer <vtkPolyData>::New();
  this->AxisRotationOutlinePolyData->SetPoints(rotationPts);
  this->AxisRotationOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->AxisRotationOutlinePolyData->InsertNextCell(VTK_POLY_LINE, rotationLine);

  this->AxisRotationOutlineCalculator = vtkSmartPointer<vtkArrayCalculator>::New();
  this->AxisRotationOutlineCalculator->SetInputConnection(this->RotationScaleTransformFilter->GetOutputPort());
  this->AxisRotationOutlineCalculator->SetResultArrayName("colorIndex");
  this->AxisRotationOutlineCalculator->SetResultArrayType(VTK_ID_TYPE);
  this->AxisRotationOutlineCalculator->AddScalarVariable("colorIndex", "colorIndex");
  this->AxisRotationOutlineCalculator->SetFunction("colorIndex+1");

  this->AxisRotationOutlineGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisRotationOutlineGlypher->SetInputConnection(this->AxisRotationOutlineCalculator->GetOutputPort());
  this->AxisRotationOutlineGlypher->SetSourceData(this->AxisRotationOutlinePolyData);
  this->AxisRotationOutlineGlypher->ScalingOff();
  this->AxisRotationOutlineGlypher->ExtractEigenvaluesOff();
  this->AxisRotationOutlineGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  vtkNew<vtkPoints> translationHandlePoints;
  vtkNew<vtkIdList> translationHandlePoly;
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint( 0.00,  0.00, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_LENGTH, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_LENGTH, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(
    -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));

  vtkNew<vtkIdList> translationHandleLine;
  translationHandleLine->DeepCopy(translationHandlePoly);
  translationHandleLine->InsertNextId(0);

  this->AxisTranslationPolyData = vtkSmartPointer <vtkPolyData>::New();
  this->AxisTranslationPolyData->SetPoints(translationHandlePoints);
  this->AxisTranslationPolyData->SetPolys(vtkNew<vtkCellArray>());
  this->AxisTranslationPolyData->InsertNextCell(VTK_POLYGON, translationHandlePoly);

  this->AxisTranslationOutlinePolyData = vtkSmartPointer <vtkPolyData>::New();
  this->AxisTranslationOutlinePolyData->SetPoints(translationHandlePoints);
  this->AxisTranslationOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->AxisTranslationOutlinePolyData->InsertNextCell(VTK_POLY_LINE, translationHandleLine);

  /// Translation pipeline
  this->TranslationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->TranslationScaleTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->TranslationScaleTransformFilter->SetInputData(this->TranslationHandlePoints);
  this->TranslationScaleTransformFilter->SetTransform(vtkNew<vtkTransform>());

  this->AxisTranslationGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisTranslationGlypher->SetInputConnection(this->TranslationScaleTransformFilter->GetOutputPort());
  this->AxisTranslationGlypher->SetSourceData(this->AxisTranslationPolyData);
  this->AxisTranslationGlypher->ScalingOff();
  this->AxisTranslationGlypher->ExtractEigenvaluesOff();
  this->AxisTranslationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  this->AxisTranslationOutlineCalculator = vtkSmartPointer<vtkArrayCalculator>::New();
  this->AxisTranslationOutlineCalculator->SetInputConnection(this->TranslationScaleTransformFilter->GetOutputPort());
  this->AxisTranslationOutlineCalculator->SetResultArrayName("colorIndex");
  this->AxisTranslationOutlineCalculator->SetResultArrayType(VTK_ID_TYPE);
  this->AxisTranslationOutlineCalculator->AddScalarVariable("colorIndex", "colorIndex");
  this->AxisTranslationOutlineCalculator->SetFunction("colorIndex+1");

  this->AxisTranslationOutlineGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisTranslationOutlineGlypher->SetInputConnection(this->AxisTranslationOutlineCalculator->GetOutputPort());
  this->AxisTranslationOutlineGlypher->SetSourceData(this->AxisTranslationOutlinePolyData);
  this->AxisTranslationOutlineGlypher->ScalingOff();
  this->AxisTranslationOutlineGlypher->ExtractEigenvaluesOff();
  this->AxisTranslationOutlineGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  /// Scale pipeline
  vtkNew<vtkEllipseArcSource> scaleArcSource;
  scaleArcSource->SetMajorRadiusVector(INTERACTION_HANDLE_RADIUS, 0.0, 0.0);
  scaleArcSource->SetResolution(100);
  scaleArcSource->SetCenter(0.0, 0.0, 0.0);
  scaleArcSource->SetNormal(0, 0, 1);
  scaleArcSource->SetRatio(1.0);
  scaleArcSource->SetStartAngle(0);
  scaleArcSource->SetSegmentAngle(360);
  scaleArcSource->Update();

  vtkNew<vtkPoints> scalePoints;
  vtkNew<vtkIdList> scalePoly;
  vtkNew<vtkIdList> scaleLine;
  for (int i = 0; i < scaleArcSource->GetOutput()->GetNumberOfPoints(); ++i)
    {
    double point[3];
    scaleArcSource->GetOutput()->GetPoint(i, point);
    vtkIdType id = scalePoints->InsertNextPoint(point);
    scalePoly->InsertNextId(id);
    scaleLine->InsertNextId(id);
    }
  scaleLine->InsertNextId(0);

  this->AxisScalePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->AxisScalePolyData->SetPoints(scalePoints);
  this->AxisScalePolyData->SetPolys(vtkNew<vtkCellArray>());
  this->AxisScalePolyData->InsertNextCell(VTK_POLYGON, scalePoly);

  this->AxisScaleOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->AxisScaleOutlinePolyData->SetPoints(scalePoints);
  this->AxisScaleOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->AxisScaleOutlinePolyData->InsertNextCell(VTK_POLY_LINE, scaleLine);

  this->ScaleHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->ScaleScaleTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ScaleScaleTransformFilter->SetInputData(this->ScaleHandlePoints);
  this->ScaleScaleTransformFilter->SetTransform(vtkNew<vtkTransform>());

  this->AxisScaleGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisScaleGlypher->SetInputConnection(this->ScaleScaleTransformFilter->GetOutputPort());
  this->AxisScaleGlypher->SetSourceData(this->AxisScalePolyData);
  this->AxisScaleGlypher->ScalingOff();
  this->AxisScaleGlypher->ExtractEigenvaluesOff();
  this->AxisScaleGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  this->AxisScaleOutlineCalculator = vtkSmartPointer<vtkArrayCalculator>::New();
  this->AxisScaleOutlineCalculator->SetInputConnection(this->ScaleScaleTransformFilter->GetOutputPort());
  this->AxisScaleOutlineCalculator->SetResultArrayName("colorIndex");
  this->AxisScaleOutlineCalculator->SetResultArrayType(VTK_ID_TYPE);
  this->AxisScaleOutlineCalculator->AddScalarVariable("colorIndex", "colorIndex");
  this->AxisScaleOutlineCalculator->SetFunction("colorIndex+1");

  this->AxisScaleOutlineGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisScaleOutlineGlypher->SetInputConnection(this->AxisScaleOutlineCalculator->GetOutputPort());
  this->AxisScaleOutlineGlypher->SetSourceData(this->AxisScaleOutlinePolyData);
  this->AxisScaleOutlineGlypher->ScalingOff();
  this->AxisScaleOutlineGlypher->ExtractEigenvaluesOff();
  this->AxisScaleOutlineGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array

  this->Append = vtkSmartPointer<vtkAppendPolyData>::New();
  this->Append->AddInputConnection(this->AxisTranslationGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisTranslationOutlineGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisRotationGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisRotationOutlineGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisScaleGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisScaleOutlineGlypher->GetOutputPort());

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
  this->Property->SetLineWidth(2.0);

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

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->RotationHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->RotationHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateTranslationHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1.0, 0.0, 0.0); // X-axis
  points->InsertNextPoint(0.0, 1.0, 0.0); // Y-axis
  points->InsertNextPoint(0.0, 0.0, 1.0); // Z-axis
  points->InsertNextPoint(0.0, 0.0, 0.0); // Free translation
  this->Pipeline->TranslationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 1.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0); // Free translation
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkDoubleArray> glyphIndexArray;
  glyphIndexArray->SetName("glyphIndex");
  glyphIndexArray->SetNumberOfComponents(1);
  glyphIndexArray->InsertNextTuple1(0); // Arrow
  glyphIndexArray->InsertNextTuple1(0);
  glyphIndexArray->InsertNextTuple1(0);
  glyphIndexArray->InsertNextTuple1(1); // Point
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(glyphIndexArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->TranslationHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandlesFacingCamera(int type)
{

}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateTranslationHandleOrientation()
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(InteractionTranslationHandle);
  if (!handlePolyData)
    {
    return;
    }

  vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(handlePolyData->GetPointData()->GetAbstractArray("orientation"));
  if (!orientationArray)
    {
    return;
    }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  double viewDirection_Handle[3] = { 0.0, 0.0, 0.0 };

  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    }
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);

  for (int i = 0; i < 3; ++i)
    {
    double xAxis[3] = { 0.0, 0.0, 0.0 };
    double yAxis[3] = { 0.0, 0.0, 0.0 };
    double zAxis[3] = { 0.0, 0.0, 0.0 };
    xAxis[i] = 1.0;

    vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
    vtkMath::Normalize(yAxis);

    vtkMath::Cross(xAxis, yAxis, zAxis);
    vtkMath::Normalize(zAxis);

    orientationArray->SetTuple9(i, xAxis[0], xAxis[1], xAxis[2],
                                   yAxis[0], yAxis[1], yAxis[2],
                                   zAxis[0], zAxis[1], zAxis[2]);
    }
  orientationArray->SetTuple9(3, 0.0, 0.0, 0.0,
                                 0.0, 0.0, 0.0,
                                 0.0, 0.0, 0.0);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateScaleHandleOrientation()
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(InteractionScaleHandle);
  if (!handlePolyData)
    {
    return;
    }

  vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(handlePolyData->GetPointData()->GetAbstractArray("orientation"));
  if (!orientationArray)
    {
    return;
    }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  double viewDirection_Handle[3] = { 0.0, 0.0, 0.0 };

  double viewUp_World[3] = { 0.0, 1.0, 0.0 };
  double viewUp_Handle[3] = { 0.0, 0.0, 0.0 };

  if (this->GetSliceNode())
    {
    this->SlicePlane->GetNormal(viewDirection_World);
    double viewup[4] = { 0,1,0,0 };
    double viewup2[4] = { 0,1,0,0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(viewup, viewup2);
    viewUp_World[0] = viewup2[0];
    viewUp_World[1] = viewup2[1];
    viewUp_World[2] = viewup2[2];
    }
  else
    {
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(viewDirection_World);
    this->Renderer->GetActiveCamera()->GetViewUp(viewUp_World);
    }
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewDirection_World, viewDirection_Handle);
  vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse())->TransformVector(viewUp_World, viewUp_Handle);

  for (int i = 0; i < orientationArray->GetNumberOfTuples(); ++i)
    {
    double xAxis[3] = { 1.0, 0.0, 0.0 };
    double yAxis[3] = { 0.0, 1.0, 0.0 };
    double zAxis[3] = { 0.0, 0.0, 1.0 };
    zAxis[0] = viewDirection_Handle[0];
    zAxis[1] = viewDirection_Handle[1];
    zAxis[2] = viewDirection_Handle[2];
    vtkMath::Normalize(zAxis);

    vtkMath::Cross(viewUp_Handle, viewDirection_Handle, xAxis);
    vtkMath::Normalize(xAxis);

    vtkMath::Cross(viewDirection_Handle, xAxis, yAxis);
    vtkMath::Normalize(yAxis);

    orientationArray->SetTuple9(i, xAxis[0], xAxis[1], xAxis[2],
                                   yAxis[0], yAxis[1], yAxis[2],
                                   zAxis[0], zAxis[1], zAxis[2]);
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateScaleHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1.5, 0.0, 0.0); // X-axis
  points->InsertNextPoint(0.0, 1.5, 0.0); // Y-axis
  points->InsertNextPoint(0.0, 0.0, 1.5); // Z-axis
  this->Pipeline->ScaleHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 1.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0,
                                     0.0, 0.0, 0.0); // Free translation
  this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->ScaleHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->ScaleHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetNumberOfHandles()
{
  int numberOfHandles = 0;
  for (int type = InteractionNone+1; type < Interaction_Last; ++type)
    {
    numberOfHandles += this->GetNumberOfHandles(type);
    }
  return numberOfHandles;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetNumberOfHandles(int type)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    vtkErrorMacro( << "GetNumberOfHandles: Invalid handle type: " << type);
    return 0;
    }
  return handlePolyData->GetNumberOfPoints();
}

//----------------------------------------------------------------------
vtkPolyData* vtkMRMLInteractionWidgetRepresentation::GetHandlePolydata(int type)
{
  if (type == InteractionRotationHandle)
    {
    return this->Pipeline->RotationHandlePoints;
    }
  else if (type == InteractionTranslationHandle)
    {
    return this->Pipeline->TranslationHandlePoints;
    }
  else if (type == InteractionScaleHandle)
    {
    return this->Pipeline->ScaleHandlePoints;
    }
  return nullptr;
}

//----------------------------------------------------------------------
vtkTransform* vtkMRMLInteractionWidgetRepresentation::GetHandleScaleTransform(int type)
{
  vtkAbstractTransform* scaleTransform = nullptr;
  if (type == InteractionRotationHandle)
    {
    scaleTransform = this->Pipeline->RotationScaleTransformFilter->GetTransform();
    }
  else if (type == InteractionTranslationHandle)
    {
    scaleTransform = this->Pipeline->TranslationScaleTransformFilter->GetTransform();
    }
  else if (type == InteractionScaleHandle)
    {
    scaleTransform = this->Pipeline->ScaleScaleTransformFilter->GetTransform();
    }
  return vtkTransform::SafeDownCast(scaleTransform);
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::UpdateHandleColors(int type, int colorIndex)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return colorIndex;
    }

  vtkPointData* pointData = handlePolyData->GetPointData();
  if (!pointData)
    {
    return colorIndex;
    }

  vtkSmartPointer<vtkFloatArray> colorArray = vtkFloatArray::SafeDownCast(
    pointData->GetAbstractArray("colorIndex"));
  if (!colorArray)
    {
    colorArray = vtkSmartPointer<vtkFloatArray>::New();
    colorArray->SetName("colorIndex");
    colorArray->SetNumberOfComponents(1);
    pointData->AddArray(colorArray);
    pointData->SetActiveScalars("colorIndex");
    }
  colorArray->Initialize();
  colorArray->SetNumberOfTuples(handlePolyData->GetNumberOfPoints()*2);

  double color[4] = { 0.0, 0.0, 0.0, 0.0 };
  for (int i = 0; i < handlePolyData->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(type, i, color);
    this->Pipeline->ColorTable->SetTableValue(colorIndex, color);
    colorArray->SetTuple1(i, colorIndex);
    ++colorIndex;

    double grey = 0.3;
    bool selected = this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == i;
    if (selected)
      {
      grey = 0.0;
      }
    this->Pipeline->ColorTable->SetTableValue(colorIndex, grey, grey, grey, color[3]);
    ++colorIndex;
    }

  return colorIndex;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleColors()
{
  int numberOfHandles = this->GetNumberOfHandles();
  int numberOfColors = numberOfHandles * 2; // alternate fill and outline colors
  this->Pipeline->ColorTable->SetNumberOfTableValues(numberOfColors);
  this->Pipeline->ColorTable->SetTableRange(0, double(numberOfColors) - 1);

  int colorIndex = 0;
  colorIndex = this->UpdateHandleColors(InteractionRotationHandle, colorIndex);
  colorIndex = this->UpdateHandleColors(InteractionTranslationHandle, colorIndex);
  colorIndex = this->UpdateHandleColors(InteractionScaleHandle, colorIndex);

  this->Pipeline->ColorTable->Build();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
    {
    return;
    }

  double red[3]    = { 0.80, 0.35, 0.35 };
  double redSelected[3] = { 0.70, 0.07, 0.07 };

  double green[4] = { 0.35, 0.80, 0.35 };
  double greenSelected[4] = { 0.07, 0.70, 0.07 };

  double blue[4]   = { 0.35, 0.35, 0.8 };
  double blueSelected[4] = { 0.07, 0.07, 0.70 };

  double orange[4] = { 0.80, 0.65, 0.35 };
  double orangeSelected[4] = { 0.70, 0.25, 0.07 };

  double white[4]  = { 0.80, 0.80, 0.80 };
  double whiteSelected[4] = { 1.00, 1.00, 1.00 };

  bool selected = this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index;

  double* currentColor = white;
  switch (index)
    {
    case 0:
      currentColor = selected ? redSelected : red;
      break;
    case 1:
      currentColor = selected ? greenSelected : green;
      break;
    case 2:
      currentColor = selected ? blueSelected : blue;
      break;
    case 3:
      currentColor = selected ? orangeSelected : orange;
      break;
    default:
      currentColor = selected ? whiteSelected : white;
      break;
    }

  double opacity = this->GetHandleOpacity(type, index);
  if (selected)
    {
    opacity = 1.0;
    }

  for (int i = 0; i < 3; ++i)
    {
    color[i] = currentColor[i];
    }
  color[3] = opacity;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetHandleVisibility(int type, int index)
{
  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  vtkIdTypeArray* visibilityArray = nullptr;
  if (handlePolyData)
    {
    visibilityArray = vtkIdTypeArray::SafeDownCast(handlePolyData->GetPointData()->GetArray("visibility"));
    }
  if (visibilityArray && index < visibilityArray->GetNumberOfValues())
    {
     return visibilityArray->GetValue(index) != 0;
    }
  return true;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetHandleOpacity(int type, int index)
{
  // Determine if the handle should be displayed
  bool handleVisible = this->GetHandleVisibility(type, index);
  if (!handleVisible)
    {
    return 0.0;
    }

  double opacity = 1.0;

  double axis_World[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisWorld(type, index, axis_World);
  if (axis_World[0] == 0.0 && axis_World[1] == 0.0 && axis_World[2] == 0.0)
    {
    // No axis specified. The handle should be viewable from any direction.
    return opacity;
    }

  double viewNormal_World[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToCameraVector(viewNormal_World);
  if (vtkMath::Dot(viewNormal_World, axis_World) < 0.0)
    {
    vtkMath::MultiplyScalar(axis_World, -1.0);
    }

  double fadeAngleRange = this->StartFadeAngleDegrees - this->EndFadeAngleDegrees;
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal_World, axis_World));
  if (type == InteractionRotationHandle)
    {
    // Fade happens when the axis approaches 90 degrees from the view normal
    if (angle > 90.0 - this->EndFadeAngleDegrees)
      {
      opacity = 0.0;
      }
    else if (angle > 90.0 - this->StartFadeAngleDegrees)
      {
      double difference = angle - (90.0 - this->StartFadeAngleDegrees);
      opacity = 1.0 - (difference / fadeAngleRange);
      }
    }
  else if (type == InteractionTranslationHandle || type == InteractionScaleHandle)
    {
    // Fade happens when the axis approaches 0 degrees from the view normal
    if (angle < this->EndFadeAngleDegrees)
      {
      opacity = 0.0;
      }
    else if (angle < this->StartFadeAngleDegrees)
      {
      double difference = angle - this->EndFadeAngleDegrees;
      opacity = (difference / fadeAngleRange);
      }
    }
  return opacity;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleToCameraVector(double normal[3])
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
    if (camera->GetParallelProjection())
      {
      camera->GetViewPlaneNormal(normal);
      }
    else
      {
      camera->GetPosition(normal);
      vtkMath::Subtract(normal, this->GetHandleToWorldTransform()->TransformPoint(0, 0, 0), normal);
      vtkMath::Normalize(normal);
      }
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetWidgetScale(double scale)
{
  vtkNew<vtkTransform> scaleTransform;
  scaleTransform->Scale(scale, scale, scale);

  this->Pipeline->RotationScaleTransformFilter->SetTransform(scaleTransform);
  this->Pipeline->AxisRotationGlypher->SetScaleFactor(scale);
  this->Pipeline->AxisRotationOutlineGlypher->SetScaleFactor(scale);

  this->Pipeline->TranslationScaleTransformFilter->SetTransform(scaleTransform);
  this->Pipeline->AxisTranslationGlypher->SetScaleFactor(scale);
  this->Pipeline->AxisTranslationOutlineGlypher->SetScaleFactor(scale);

  this->Pipeline->ScaleScaleTransformFilter->SetTransform(scaleTransform);
  this->Pipeline->AxisScaleGlypher->SetScaleFactor(scale);
  this->Pipeline->AxisScaleOutlineGlypher->SetScaleFactor(scale);
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
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisLocal(int type, int index, double axisLocal[3])
{
  if (!axisLocal)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  axisLocal[0] = 0.0;
  axisLocal[1] = 0.0;
  axisLocal[2] = 0.0;

  switch (index)
    {
    case 0:
      axisLocal[0] = 1.0;
      break;
    case 1:
      axisLocal[1] = 1.0;
      break;
    case 2:
      axisLocal[2] = 1.0;
      break;
    default:
      break;
    }
}


//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisWorld(int type, int index, double axisWorld[3])
{
  if (!axisWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleVectorWorld: Invalid axis argument");
    return;
    }

  double origin[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisLocal(type, index, axisWorld);
  this->Pipeline->HandleToWorldTransform->TransformVectorAtPoint(origin, axisWorld, axisWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandlePositionLocal(int type, int index, double positionLocal[3])
{
  if (!positionLocal)
    {
    vtkErrorMacro("GetInteractionHandlePositionLocal: Invalid position argument");
    return;
    }

  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return;
    }

  if (index < 0 || index >= handlePolyData->GetNumberOfPoints())
    {
    return;
    }

  handlePolyData->GetPoint(index, positionLocal);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandlePositionWorld(int type, int index, double positionWorld[3])
{
  if (!positionWorld)
    {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandlePositionWorld: Invalid position argument");
    }

  vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
  if (!handlePolyData)
    {
    return;
    }
  handlePolyData->GetPoint(index, positionWorld);

  vtkTransform* handleScaleTransform = this->GetHandleScaleTransform(type);
  if (handleScaleTransform)
    {
    handleScaleTransform->TransformPoint(positionWorld, positionWorld);
    }

  this->Pipeline->HandleToWorldTransform->TransformPoint(positionWorld, positionWorld);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::HandleInfo vtkMRMLInteractionWidgetRepresentation::GetHandleInfo(int type, int index)
{
  double handlePositionLocal[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandlePositionLocal(type, index, handlePositionLocal);

  double handlePositionWorld[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandlePositionWorld(type, index, handlePositionWorld);

  double color[4] = { 0.0, 0.0, 0.0, 0.0 };
  this->GetHandleColor(type, index, color);

  return HandleInfo(index, type, handlePositionWorld, handlePositionLocal, color);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::HandleInfoList vtkMRMLInteractionWidgetRepresentation::GetHandleInfoList()
{
  HandleInfoList handleInfoList;

  for (int index = 0; index < this->GetNumberOfHandles(InteractionRotationHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionRotationHandle, index));
    }

  for (int index = 0; index < this->GetNumberOfHandles(InteractionTranslationHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionTranslationHandle, index));
    }

  for (int index = 0; index < this->GetNumberOfHandles(InteractionScaleHandle); ++index)
    {
    handleInfoList.push_back(this->GetHandleInfo(InteractionScaleHandle, index));
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
void vtkMRMLInteractionWidgetRepresentation::UpdateSlicePlaneFromSliceNode()
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
  double normal[3] = { 0.0, 0.0, 0.0 };
  double origin[3] = { 0.0, 0.0, 0.0 };
  const double planeOrientation = 1.0; // +/-1: orientation of the normal
  for (int i = 0; i < 3; i++)
    {
    normal[i] = planeOrientation * sliceXYToRAS->GetElement(i, 2);
    origin[i] = sliceXYToRAS->GetElement(i, 3);
    }
  vtkMath::Normalize(normal);

  // Compare slice normal and new normal
  double normalDifferenceAngle = vtkMath::AngleBetweenVectors(normal, this->SlicePlane->GetNormal());
  double originDifferenceMm = vtkMath::Distance2BetweenPoints(origin, this->SlicePlane->GetOrigin());
  double epsilon = 1e-6;
  if (normalDifferenceAngle < epsilon && originDifferenceMm < epsilon)
    {
    // No change in slice plane
    return;
    }

  this->SlicePlane->SetNormal(normal);
  this->SlicePlane->SetOrigin(origin);
  this->SlicePlane->Modified();
  this->NeedToRenderOn();
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
    double cameraFP[3] = { 0.0, 0.0, 0.0 };
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

//----------------------------------------------------------------------
vtkTransform* vtkMRMLInteractionWidgetRepresentation::GetHandleToWorldTransform()
{
  if (!this->Pipeline)
    {
    return nullptr;
    }
  return this->Pipeline->HandleToWorldTransform;
}
