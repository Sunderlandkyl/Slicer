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
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

// VTK includes
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkDoubleArray.h>
#include <vtkEllipseArcSource.h>
#include <vtkFloatArray.h>
#include <vtkFocalPlanePointPlacer.h>
#include <vtkLine.h>
#include <vtkMath.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLViewNode.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkTransform.h>

// MRML includes
#include <vtkMRMLAbstractThreeDViewDisplayableManager.h>
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLTransformNode.h>

#include <vtkMRMLInteractionWidgetRepresentation.h>

#include <map>
#include <set>

//----------------------------------------------------------------------
// Shared handle renderer: one per vtkRenderer, collects all interaction
// handle instances from all representations and renders them in a single
// GPU draw call (eliminating per-actor state-setup overhead).
// Only used for 3D views; slice views keep per-representation rendering
// because each representation has a unique WorldToSliceTransform Z-offset.
//----------------------------------------------------------------------
namespace {

struct SharedHandleRenderer
{
  vtkSmartPointer<vtkPolyData> InstancePolyData;
  vtkSmartPointer<vtkFloatArray> GlyphOrientationArray;
  vtkSmartPointer<vtkFloatArray> GlyphScaleArray;
  vtkSmartPointer<vtkIntArray> GlyphSourceIndexArray;
  vtkSmartPointer<vtkUnsignedCharArray> GlyphMaskArray;
  vtkSmartPointer<vtkUnsignedCharArray> ColorArray;

  vtkSmartPointer<vtkGlyph3DMapper> Mapper;
  vtkSmartPointer<vtkProperty> Property;
  vtkSmartPointer<vtkActor> Actor;

  std::set<vtkMRMLInteractionWidgetRepresentation*> Representations;
  bool OpaqueRendered{ false };
  bool OverlayRendered{ false };
  bool GlyphSourcesInitialized{ false };
  unsigned long StartEventTag{ 0 };
  unsigned long DeleteEventTag{ 0 };
};

std::map<vtkRenderer*, SharedHandleRenderer*> g_SharedRenderers;

void OnSharedRendererStartEvent(vtkObject*, unsigned long, void* clientData, void*)
{
  auto* shared = static_cast<SharedHandleRenderer*>(clientData);
  shared->OpaqueRendered = false;
  shared->OverlayRendered = false;
}

void OnSharedRendererDelete(vtkObject* caller, unsigned long, void*, void*)
{
  auto* renderer = static_cast<vtkRenderer*>(caller);
  auto it = g_SharedRenderers.find(renderer);
  if (it != g_SharedRenderers.end())
  {
    delete it->second;
    g_SharedRenderers.erase(it);
  }
}

SharedHandleRenderer* GetOrCreateSharedRenderer(vtkRenderer* renderer)
{
  if (!renderer)
  {
    return nullptr;
  }

  auto it = g_SharedRenderers.find(renderer);
  if (it != g_SharedRenderers.end())
  {
    return it->second;
  }

  auto* shared = new SharedHandleRenderer();
  g_SharedRenderers[renderer] = shared;

  shared->InstancePolyData = vtkSmartPointer<vtkPolyData>::New();
  shared->InstancePolyData->SetPoints(vtkSmartPointer<vtkPoints>::New());

  shared->GlyphOrientationArray = vtkSmartPointer<vtkFloatArray>::New();
  shared->GlyphOrientationArray->SetName("orientation");
  shared->GlyphOrientationArray->SetNumberOfComponents(4);
  shared->InstancePolyData->GetPointData()->AddArray(shared->GlyphOrientationArray);

  shared->GlyphScaleArray = vtkSmartPointer<vtkFloatArray>::New();
  shared->GlyphScaleArray->SetName("scale");
  shared->GlyphScaleArray->SetNumberOfComponents(1);
  shared->InstancePolyData->GetPointData()->AddArray(shared->GlyphScaleArray);

  shared->GlyphSourceIndexArray = vtkSmartPointer<vtkIntArray>::New();
  shared->GlyphSourceIndexArray->SetName("glyphType");
  shared->GlyphSourceIndexArray->SetNumberOfComponents(1);
  shared->InstancePolyData->GetPointData()->AddArray(shared->GlyphSourceIndexArray);

  shared->GlyphMaskArray = vtkSmartPointer<vtkUnsignedCharArray>::New();
  shared->GlyphMaskArray->SetName("mask");
  shared->GlyphMaskArray->SetNumberOfComponents(1);
  shared->InstancePolyData->GetPointData()->AddArray(shared->GlyphMaskArray);

  shared->ColorArray = vtkSmartPointer<vtkUnsignedCharArray>::New();
  shared->ColorArray->SetName("color");
  shared->ColorArray->SetNumberOfComponents(4);
  shared->InstancePolyData->GetPointData()->AddArray(shared->ColorArray);

  shared->Mapper = vtkSmartPointer<vtkGlyph3DMapper>::New();
  shared->Mapper->SetInputData(shared->InstancePolyData);
  shared->Mapper->SetSourceIndexing(true);
  shared->Mapper->SetSourceIndexArray("glyphType");
  shared->Mapper->OrientOn();
  shared->Mapper->SetOrientationModeToQuaternion();
  shared->Mapper->SetOrientationArray("orientation");
  shared->Mapper->ScalingOn();
  shared->Mapper->SetScaleModeToScaleByMagnitude();
  shared->Mapper->SetScaleArray("scale");
  shared->Mapper->SetMasking(true);
  shared->Mapper->SetMaskArray("mask");
  shared->Mapper->SetColorModeToDirectScalars();
  shared->Mapper->ScalarVisibilityOn();
  shared->Mapper->SetScalarModeToUsePointFieldData();
  shared->Mapper->SelectColorArray("color");

  shared->Property = vtkSmartPointer<vtkProperty>::New();
  shared->Property->SetPointSize(1.e-6);
  shared->Property->SetLineWidth(1.0);
  shared->Property->SetDiffuse(0.0);
  shared->Property->SetAmbient(1.0);
  shared->Property->SetMetallic(0.0);
  shared->Property->SetSpecular(0.0);
  shared->Property->SetEdgeVisibility(false);
  shared->Property->SetOpacity(1.0);

  shared->Actor = vtkSmartPointer<vtkActor>::New();
  shared->Actor->SetProperty(shared->Property);
  shared->Actor->SetMapper(shared->Mapper);
  shared->Actor->ForceOpaqueOn();

  vtkNew<vtkCallbackCommand> startCB;
  startCB->SetCallback(OnSharedRendererStartEvent);
  startCB->SetClientData(shared);
  shared->StartEventTag = renderer->AddObserver(vtkCommand::StartEvent, startCB.Get());

  vtkNew<vtkCallbackCommand> deleteCB;
  deleteCB->SetCallback(OnSharedRendererDelete);
  shared->DeleteEventTag = renderer->AddObserver(vtkCommand::DeleteEvent, deleteCB.Get());

  return shared;
}

void UnregisterFromAllSharedRenderers(vtkMRMLInteractionWidgetRepresentation* rep)
{
  for (auto& pair : g_SharedRenderers)
  {
    if (pair.second->Representations.erase(rep) > 0)
    {
      pair.second->GlyphSourcesInitialized = false;
    }
  }
}

} // anonymous namespace

//----------------------------------------------------------------------
static const double INTERACTION_HANDLE_SCALE_RADIUS = 0.1;
static const double INTERACTION_HANDLE_SCALE_DISTANCE_FROM_CENTER = 1.6;

static const double INTERACTION_HANDLE_ROTATION_ARC_THICKNESS = 0.1;
static const double INTERACTION_HANDLE_RADIUS = 1.15;
static const double INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS = INTERACTION_HANDLE_RADIUS + 0.5 * INTERACTION_HANDLE_ROTATION_ARC_THICKNESS;
static const double INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS = INTERACTION_HANDLE_RADIUS - 0.5 * INTERACTION_HANDLE_ROTATION_ARC_THICKNESS;
static const double INTERACTION_HANDLE_ROTATION_ARC_DEGREES = 360.0;
static const int INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION = 30;

static const double INTERACTION_TRANSLATION_HANDLE_LENGTH = 0.75;
static const double INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS = 0.15;
static const double INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS = 0.05;

// Crosshair glyph: four short bars pointing away from the center, with a gap in the middle
// so that whatever is underneath the handle position remains visible. Rendered as thin filled
// quads (polygons), not as line primitives: VTK's "always on top" depth-offset trick used for the
// interaction handles is applied much less reliably to line primitives than to polygons, which made
// a line-based crosshair disappear intermittently (e.g. when another transform's handle was also
// visible in the same slice view).
static const double INTERACTION_HANDLE_CROSSHAIR_INNER_RADIUS = 0.10;
static const double INTERACTION_HANDLE_CROSSHAIR_OUTER_RADIUS = 0.28;
static const double INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH = 0.025;

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::vtkMRMLInteractionWidgetRepresentation()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000;

  this->NeedToRender = false;

  this->PointPlacer = vtkSmartPointer<vtkFocalPlanePointPlacer>::New();

  this->AlwaysOnTop = true;

  // Using the minimum value of -66000 creates a lot of rendering artifacts on the occluded objects, as all of the
  // pixels in the occluded object will have the same depth buffer value (0.0).
  // Using a default value of -25000 strikes a balance between rendering the occluded objects on top of other objects,
  // while still providing enough leeway to ensure that occluded actors are rendered correctly relative to themselves
  // and to other occluded actors.
  this->AlwaysOnTopRelativeOffsetUnits = -25000.0;

  this->Pipeline = nullptr;

  this->SlicePlane = vtkSmartPointer<vtkPlane>::New();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetupInteractionPipeline()
{
  this->Pipeline = new InteractionPipeline();
  this->InitializePipeline();
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::~vtkMRMLInteractionWidgetRepresentation()
{
  UnregisterFromAllSharedRenderers(this);

  // Force deleting variables to prevent circular dependency keeping objects alive
  if (this->Pipeline != nullptr)
  {
    delete this->Pipeline;
    this->Pipeline = nullptr;
  }
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetMaximumHandlePickingDistance2()
{
  double maximumHandlePickingDistance = this->InteractionSize / 10.0 + this->PickingTolerance * this->GetScreenScaleFactor();
  return maximumHandlePickingDistance * maximumHandlePickingDistance;
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteract(vtkMRMLInteractionEventData* interactionEventData,
                                                         int& foundComponentType,
                                                         int& foundComponentIndex,
                                                         double& closestDistance2)
{
  foundComponentType = InteractionNone;
  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system
  foundComponentIndex = -1;

  vtkMRMLAbstractViewNode* viewNode = this->GetViewNode();
  if (!viewNode || !this->IsDisplayable() || !interactionEventData)
  {
    return;
  }

  HandleInfoList handleInfoList = this->GetHandleInfoList();
  for (HandleInfo handleInfo : handleInfoList)
  {
    if (!handleInfo.IsVisible())
    {
      continue;
    }

    if (handleInfo.GlyphType == GlyphCircle || handleInfo.GlyphType == GlyphCrosshair)
    {
      this->CanInteractWithCircleHandle(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2, handleInfo);
    }
    else if (handleInfo.GlyphType == GlyphRing)
    {
      this->CanInteractWithRingHandle(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2, handleInfo);
    }
    else if (handleInfo.GlyphType == GlyphArrow)
    {
      this->CanInteractWithArrowHandle(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2, handleInfo);
    }
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteractWithCircleHandle(vtkMRMLInteractionEventData* interactionEventData,
                                                                         int& foundComponentType,
                                                                         int& foundComponentIndex,
                                                                         double& closestDistance2,
                                                                         HandleInfo& handleInfo)
{
  vtkMRMLAbstractViewNode* viewNode = this->GetViewNode();
  if (!viewNode || !this->IsDisplayable() || !interactionEventData || !handleInfo.IsVisible() || !interactionEventData->IsDisplayPositionValid())
  {
    return;
  }

  double maxPickingDistanceFromControlPoint2 = this->GetMaximumHandlePickingDistance2();
  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  const int* displayPosition = interactionEventData->GetDisplayPosition();
  displayPosition3[0] = static_cast<double>(displayPosition[0]);
  displayPosition3[1] = static_cast<double>(displayPosition[1]);

  double handleDisplayPos[4] = { 0.0, 0.0, 0.0, 1.0 };
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (sliceNode)
  {
    vtkNew<vtkMatrix4x4> rasToxyMatrix;
    vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);

    double* handleWorldPos = handleInfo.PositionWorld;
    rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
    handleDisplayPos[2] = 0.0; // Handles are always projected
  }
  else
  {
    double* handleWorldPos = handleInfo.PositionWorld;
    this->Renderer->SetWorldPoint(handleWorldPos);
    this->Renderer->WorldToDisplay();
    this->Renderer->GetDisplayPoint(handleDisplayPos);
    handleDisplayPos[2] = 0.0; // Handles are always projected
  }

  double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
  if (dist2 < maxPickingDistanceFromControlPoint2 && dist2 <= closestDistance2)
  {
    closestDistance2 = dist2;
    foundComponentType = handleInfo.ComponentType;
    foundComponentIndex = handleInfo.Index;
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteractWithArrowHandle(vtkMRMLInteractionEventData* interactionEventData,
                                                                        int& foundComponentType,
                                                                        int& foundComponentIndex,
                                                                        double& closestDistance2,
                                                                        HandleInfo& handleInfo)
{
  if (!handleInfo.IsVisible() || !interactionEventData->IsDisplayPositionValid())
  {
    return;
  }

  // Check the arrow tip.
  this->CanInteractWithCircleHandle(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2, handleInfo);

  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  displayPosition3[0] = static_cast<double>(displayPosition[0]);
  displayPosition3[1] = static_cast<double>(displayPosition[1]);

  double* handleWorldPos = handleInfo.PositionWorld;
  double handleWorldPos4[4] = { handleWorldPos[0], handleWorldPos[1], handleWorldPos[2], 1.0 };
  double handleDisplayPos4[4] = { 0.0, 0.0, 0.0, 1.0 };
  double originWorldPos4[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->GetInteractionHandleOriginWorld(originWorldPos4);
  double originDisplayPos4[4] = { 0.0, 0.0, 0.0, 1.0 };

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (sliceNode)
  {
    vtkNew<vtkMatrix4x4> rasToxyMatrix;
    vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);

    rasToxyMatrix->MultiplyPoint(handleWorldPos4, handleDisplayPos4);
    handleDisplayPos4[2] = 0.0; // Handles are always projected.

    rasToxyMatrix->MultiplyPoint(originWorldPos4, originDisplayPos4);
    originDisplayPos4[2] = 0.0; // Handles are always projected.
  }
  else
  {
    this->Renderer->SetWorldPoint(handleWorldPos);
    this->Renderer->WorldToDisplay();
    this->Renderer->GetDisplayPoint(handleDisplayPos4);
    handleDisplayPos4[2] = 0.0; // Handles are always projected.

    this->Renderer->SetWorldPoint(originWorldPos4);
    this->Renderer->WorldToDisplay();
    this->Renderer->GetDisplayPoint(originDisplayPos4);
    originDisplayPos4[2] = 0.0; // Handles are always projected.
  }

  double t = 0.0;
  double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos4, handleDisplayPos4, t);
  if (t > 1.0 - INTERACTION_TRANSLATION_HANDLE_LENGTH && t < 1.0)
  {
    double lineDistance2 = lineDistance * lineDistance;
    if (lineDistance < this->GetMaximumHandlePickingDistance2() && lineDistance2 <= closestDistance2)
    {
      closestDistance2 = lineDistance2;
      foundComponentType = handleInfo.ComponentType;
      foundComponentIndex = handleInfo.Index;
    }
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CanInteractWithRingHandle(vtkMRMLInteractionEventData* interactionEventData,
                                                                       int& foundComponentType,
                                                                       int& foundComponentIndex,
                                                                       double& closestDistance2,
                                                                       HandleInfo& handleInfo)
{
  double handleNormalWorld[4] = { 0.0, 0.0, 0.0, 0.0 };
  this->GetInteractionHandleAxisWorld(handleInfo.ComponentType, handleInfo.Index, handleNormalWorld);
  if (handleNormalWorld[0] == 0.0 && handleNormalWorld[1] == 0.0 && handleNormalWorld[2] == 0.0)
  {
    // Axis not valid.
    return;
  }

  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  if (!interactionEventData->IsDisplayPositionValid())
  {
    return;
  }

  double* handleWorldPos = handleInfo.PositionWorld;

  vtkNew<vtkPlane> plane;
  plane->SetOrigin(handleWorldPos);
  plane->SetNormal(handleNormalWorld);

  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition4[4] = { 0.0, 0.0, 0.0, 1.0 };
  displayPosition4[0] = static_cast<double>(displayPosition[0]);
  displayPosition4[1] = static_cast<double>(displayPosition[1]);
  double worldPosition4[4] = { 0.0, 0.0, 0.0, 0.0 };

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (sliceNode)
  {
    sliceNode->GetXYToRAS()->MultiplyPoint(displayPosition4, worldPosition4);
  }
  else
  {
    this->Renderer->SetDisplayPoint(displayPosition4);
    this->Renderer->DisplayToWorld();
    this->Renderer->GetWorldPoint(worldPosition4);
  }

  double viewDirection_World[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToCameraVectorWorld(handleWorldPos, viewDirection_World);

  double t = 0; // not used
  double interactionPointOnPlane_World[3] = { 0.0, 0.0, 0.0 };
  double worldPositionAlongView[4] = { worldPosition4[0] + viewDirection_World[0], worldPosition4[1] + viewDirection_World[1], worldPosition4[2] + viewDirection_World[2], 1.0 };
  plane->IntersectWithLine(worldPosition4, worldPositionAlongView, t, interactionPointOnPlane_World);

  double closestPointOnRing_World[4] = { 0.0, 0.0, 0.0, 1.0 };
  vtkMath::Subtract(interactionPointOnPlane_World, handleWorldPos, closestPointOnRing_World);
  vtkMath::Normalize(closestPointOnRing_World);

  double radius = INTERACTION_HANDLE_RADIUS;
  if (handleInfo.Index == 3)
  {
    radius *= 1.2;
  }

  vtkMath::MultiplyScalar(closestPointOnRing_World, this->WidgetScale * radius);
  vtkMath::Add(handleWorldPos, closestPointOnRing_World, closestPointOnRing_World);

  double closestPointOnRing_Display4[4] = { 0.0, 0.0, 0.0, 1.0 };
  if (sliceNode)
  {
    vtkNew<vtkMatrix4x4> rasToxyMatrix;
    vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);
    rasToxyMatrix->MultiplyPoint(closestPointOnRing_World, closestPointOnRing_Display4);
  }
  else
  {
    this->Renderer->SetWorldPoint(closestPointOnRing_World);
    this->Renderer->WorldToDisplay();
    this->Renderer->GetDisplayPoint(closestPointOnRing_Display4);
  }
  closestPointOnRing_Display4[2] = 0.0;

  double dist2 = vtkMath::Distance2BetweenPoints(displayPosition4, closestPointOnRing_Display4);
  if (dist2 < this->GetMaximumHandlePickingDistance2() && dist2 <= closestDistance2)
  {
    closestDistance2 = dist2;
    foundComponentType = handleInfo.ComponentType;
    foundComponentIndex = handleInfo.Index;
  }
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetTransformationReferencePoint(double referencePointWorld[3])
{
  double origin[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToWorldTransform()->TransformPoint(origin, referencePointWorld);
  return true;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateFromMRML(vtkMRMLNode* vtkNotUsed(caller), unsigned long vtkNotUsed(event), void* vtkNotUsed(callData))
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

  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateInteractionPipeline()
{
  this->NeedToRenderOn();
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
void vtkMRMLInteractionWidgetRepresentation::UpdateInstanceArrays()
{
  int totalHandles = this->GetNumberOfHandles();
  int totalInstances = totalHandles * 2;

  vtkPoints* points = this->Pipeline->InstancePolyData->GetPoints();
  points->SetNumberOfPoints(totalInstances);
  this->Pipeline->GlyphOrientationArray->SetNumberOfTuples(totalInstances);
  this->Pipeline->GlyphScaleArray->SetNumberOfTuples(totalInstances);
  this->Pipeline->GlyphSourceIndexArray->SetNumberOfTuples(totalInstances);
  this->Pipeline->GlyphMaskArray->SetNumberOfTuples(totalInstances);
  this->Pipeline->ColorArray->SetNumberOfTuples(totalInstances);

  vtkTransform* worldToHandleTransform =
    vtkTransform::SafeDownCast(this->Pipeline->HandleToWorldTransform->GetInverse());

  double viewUp_Handle[3] = { 0.0, 1.0, 0.0 };
  if (worldToHandleTransform)
  {
    worldToHandleTransform->TransformVector(this->CachedViewUp_World, viewUp_Handle);
  }

  double viewDir_Handle_Const[3] = { 0.0, 0.0, 1.0 };
  if (this->CachedParallelProjection && worldToHandleTransform)
  {
    worldToHandleTransform->TransformVector(this->CachedCameraDirection, viewDir_Handle_Const);
  }

  // Pre-extract HandleToWorldTransform matrix for fast per-handle opacity computation
  double h2w[4][4];
  vtkMatrix4x4::DeepCopy(*h2w, this->Pipeline->HandleToWorldTransform->GetMatrix());
  double overallOpacity = this->GetInteractionHandleOpacity();
  double fadeAngleRange = this->StartFadeAngleDegrees - this->EndFadeAngleDegrees;

  bool anyVisible = false;
  int instanceIndex = 0;
  int handleTypes[] = { InteractionRotationHandle, InteractionTranslationHandle, InteractionScaleHandle };

  for (int type : handleTypes)
  {
    vtkPolyData* handlePolyData = this->GetHandlePolydata(type);
    if (!handlePolyData || !handlePolyData->GetPoints())
    {
      continue;
    }

    vtkDoubleArray* orientationArray = vtkDoubleArray::SafeDownCast(
      handlePolyData->GetPointData()->GetArray("orientation"));

    int numHandles = handlePolyData->GetNumberOfPoints();
    for (int index = 0; index < numHandles; ++index)
    {
      double point[3] = { 0.0, 0.0, 0.0 };
      handlePolyData->GetPoints()->GetPoint(index, point);
      if (this->GetApplyScaleToPosition(type, index))
      {
        vtkMath::MultiplyScalar(point, this->WidgetScale);
      }
      points->SetPoint(instanceIndex, point);

      double quat[4] = { 1.0, 0.0, 0.0, 0.0 };

      if (type == InteractionRotationHandle && index < 3)
      {
        if (orientationArray && index < orientationArray->GetNumberOfTuples())
        {
          double orient9[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
          orientationArray->GetTuple(index, orient9);
          double matrix3x3[3][3];
          for (int i = 0; i < 3; ++i)
          {
            for (int j = 0; j < 3; ++j)
            {
              matrix3x3[j][i] = orient9[i * 3 + j];
            }
          }
          vtkMath::Matrix3x3ToQuaternion(matrix3x3, quat);
        }
      }
      else
      {
        double viewDir_Handle[3];
        if (this->CachedParallelProjection)
        {
          viewDir_Handle[0] = viewDir_Handle_Const[0];
          viewDir_Handle[1] = viewDir_Handle_Const[1];
          viewDir_Handle[2] = viewDir_Handle_Const[2];
        }
        else if (worldToHandleTransform)
        {
          double posWorld[3] = {
            h2w[0][0] * point[0] + h2w[0][1] * point[1] + h2w[0][2] * point[2] + h2w[0][3],
            h2w[1][0] * point[0] + h2w[1][1] * point[1] + h2w[1][2] * point[2] + h2w[1][3],
            h2w[2][0] * point[0] + h2w[2][1] * point[1] + h2w[2][2] * point[2] + h2w[2][3]
          };
          double viewDir_World[3];
          this->GetHandleToCameraVectorWorld(posWorld, viewDir_World);
          worldToHandleTransform->TransformVector(viewDir_World, viewDir_Handle);
        }
        else
        {
          viewDir_Handle[0] = 0.0;
          viewDir_Handle[1] = 0.0;
          viewDir_Handle[2] = 1.0;
        }

        double xAxis[3], yAxis[3], zAxis[3];

        if (type == InteractionTranslationHandle && index < 3)
        {
          xAxis[0] = xAxis[1] = xAxis[2] = 0.0;
          xAxis[index] = 1.0;
          vtkMath::Cross(viewDir_Handle, xAxis, yAxis);
          vtkMath::Normalize(yAxis);
          vtkMath::Cross(xAxis, yAxis, zAxis);
          vtkMath::Normalize(zAxis);
        }
        else
        {
          zAxis[0] = viewDir_Handle[0];
          zAxis[1] = viewDir_Handle[1];
          zAxis[2] = viewDir_Handle[2];
          vtkMath::Normalize(zAxis);
          vtkMath::Cross(viewUp_Handle, viewDir_Handle, xAxis);
          vtkMath::Normalize(xAxis);
          vtkMath::Cross(viewDir_Handle, xAxis, yAxis);
          vtkMath::Normalize(yAxis);
        }

        double matrix3x3[3][3] = {
          { xAxis[0], yAxis[0], zAxis[0] },
          { xAxis[1], yAxis[1], zAxis[1] },
          { xAxis[2], yAxis[2], zAxis[2] }
        };
        vtkMath::Matrix3x3ToQuaternion(matrix3x3, quat);
      }

      this->Pipeline->GlyphOrientationArray->SetTuple4(
        instanceIndex, quat[0], quat[1], quat[2], quat[3]);

      double scale = this->WidgetScale;
      if (type == InteractionRotationHandle && index == 3)
      {
        scale *= 1.2;
      }

      // Pre-compute opacity, avoiding the expensive GetHandleOpacity virtual dispatch chain
      // (GetInteractionHandleAxisWorld → GetInteractionHandlePositionWorld).
      // GetHandleVisibility is still called virtually to respect display node per-axis settings.
      bool handleVisible = this->GetHandleVisibility(type, index);
      double precomputedOpacity = 0.0;
      if (handleVisible)
      {
        double axisWorld[3] = { 0.0, 0.0, 0.0 };
        if (type == InteractionScaleHandle)
        {
          double axisLocal[3] = { 0.0, 0.0, 0.0 };
          this->GetInteractionHandleAxisLocal(type, index, axisLocal);
          axisWorld[0] = h2w[0][0] * axisLocal[0] + h2w[0][1] * axisLocal[1] + h2w[0][2] * axisLocal[2];
          axisWorld[1] = h2w[1][0] * axisLocal[0] + h2w[1][1] * axisLocal[1] + h2w[1][2] * axisLocal[2];
          axisWorld[2] = h2w[2][0] * axisLocal[0] + h2w[2][1] * axisLocal[1] + h2w[2][2] * axisLocal[2];
        }
        else if (index < 3)
        {
          axisWorld[0] = h2w[0][index];
          axisWorld[1] = h2w[1][index];
          axisWorld[2] = h2w[2][index];
        }

        if (axisWorld[0] == 0.0 && axisWorld[1] == 0.0 && axisWorld[2] == 0.0)
        {
          precomputedOpacity = overallOpacity;
        }
        else
        {
          double posWorld_Op[3] = {
            h2w[0][0] * point[0] + h2w[0][1] * point[1] + h2w[0][2] * point[2] + h2w[0][3],
            h2w[1][0] * point[0] + h2w[1][1] * point[1] + h2w[1][2] * point[2] + h2w[1][3],
            h2w[2][0] * point[0] + h2w[2][1] * point[1] + h2w[2][2] * point[2] + h2w[2][3]
          };
          double viewNormal[3];
          this->GetHandleToCameraVectorWorld(posWorld_Op, viewNormal);
          if (vtkMath::Dot(viewNormal, axisWorld) < 0.0)
          {
            vtkMath::MultiplyScalar(axisWorld, -1.0);
          }

          double opacity = 1.0;
          double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal, axisWorld));
          if (type == InteractionRotationHandle)
          {
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
          else
          {
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
          precomputedOpacity = opacity * overallOpacity;
        }
      }

      this->CachedHandleOpacity = precomputedOpacity;
      double color[4] = { 0.0, 0.0, 0.0, 0.0 };
      this->GetHandleColor(type, index, color);
      this->CachedHandleOpacity = -1.0;
      bool visible = color[3] > 0.001;

      if (!visible)
      {
        scale = 0.0;
      }
      this->Pipeline->GlyphScaleArray->SetValue(instanceIndex, scale);
      this->Pipeline->GlyphMaskArray->SetValue(instanceIndex, visible ? 1 : 0);
      anyVisible = anyVisible || visible;

      int glyphType = this->GetHandleGlyphType(type, index);

      // Fill instance
      unsigned char fillRGBA[4] = {
        static_cast<unsigned char>(color[0] * 255),
        static_cast<unsigned char>(color[1] * 255),
        static_cast<unsigned char>(color[2] * 255),
        static_cast<unsigned char>(color[3] * 255)
      };
      this->Pipeline->ColorArray->SetTypedTuple(instanceIndex, fillRGBA);
      this->Pipeline->GlyphSourceIndexArray->SetValue(instanceIndex, glyphType);

      // Outline instance (at offset totalHandles)
      int outlineIndex = totalHandles + instanceIndex;
      points->SetPoint(outlineIndex, point);
      this->Pipeline->GlyphOrientationArray->SetTuple4(
        outlineIndex, quat[0], quat[1], quat[2], quat[3]);
      this->Pipeline->GlyphScaleArray->SetValue(outlineIndex, scale);
      this->Pipeline->GlyphMaskArray->SetValue(outlineIndex, visible ? 1 : 0);
      this->Pipeline->GlyphSourceIndexArray->SetValue(outlineIndex, glyphType + GlyphArrowOutline);

      bool selected = (this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index);
      unsigned char grey = selected ? 0 : static_cast<unsigned char>(0.3 * 255);
      unsigned char outlineRGBA[4] = { grey, grey, grey, fillRGBA[3] };
      this->Pipeline->ColorArray->SetTypedTuple(outlineIndex, outlineRGBA);

      ++instanceIndex;
    }
  }

  this->HasVisibleHandles = anyVisible;
  this->Pipeline->InstancePolyData->Modified();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateActorTransform()
{
  this->Pipeline->ActorTransform->Identity();
  this->Pipeline->ActorTransform->PostMultiply();
  this->Pipeline->ActorTransform->Concatenate(this->Pipeline->HandleToWorldTransform);

  if (this->GetSliceNode())
  {
    this->Pipeline->ActorTransform->Concatenate(this->Pipeline->WorldToSliceTransform);
  }

  this->Pipeline->Actor->SetUserTransform(this->Pipeline->ActorTransform);
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
  if (this->Renderer)
  {
    auto it = g_SharedRenderers.find(this->Renderer);
    if (it != g_SharedRenderers.end())
    {
      it->second->Actor->ReleaseGraphicsResources(window);
    }
  }
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOverlay(vtkViewport* viewport)
{
  int count = 0;
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
  {
    return count;
  }

  if (!this->GetSliceNode() && this->Renderer)
  {
    auto it = g_SharedRenderers.find(this->Renderer);
    if (it != g_SharedRenderers.end() && !it->second->OverlayRendered)
    {
      it->second->OverlayRendered = true;
      count += it->second->Actor->RenderOverlay(viewport);
    }
  }
  else
  {
    count += this->Pipeline->Actor->RenderOverlay(viewport);
  }
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  if (!this->Pipeline)
  {
    this->SetupInteractionPipeline();
  }

  int count = 0;
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
  {
    return count;
  }

  // Slice views: per-representation rendering (each rep has a unique
  // WorldToSliceTransform Z-offset so they cannot share a single actor).
  if (this->GetSliceNode())
  {
    vtkCamera* camera = this->Renderer ? this->Renderer->GetActiveCamera() : nullptr;
    vtkMTimeType cameraMTime = camera ? camera->GetMTime() : 0;
    int activeType = this->GetActiveComponentType();
    int activeIndex = this->GetActiveComponentIndex();

    bool needsUpdate = (this->GetMTime() != this->PostUpdatePipelineMTime)
      || (cameraMTime != this->LastRenderCameraMTime)
      || (activeType != this->LastRenderActiveType)
      || (activeIndex != this->LastRenderActiveIndex);

    if (needsUpdate)
    {
      this->UpdateHandleToWorldTransform();
      this->UpdateSlicePlaneFromSliceNode();
      this->UpdateCameraState();
      this->UpdateViewScaleFactor();
      this->UpdateInteractionPipeline();
      this->UpdateHandleSize();
      this->UpdateInstanceArrays();
      this->UpdateActorTransform();

      this->PostUpdatePipelineMTime = this->GetMTime();
      this->LastRenderCameraMTime = camera ? camera->GetMTime() : 0;
      this->LastRenderActiveType = activeType;
      this->LastRenderActiveIndex = activeIndex;
    }
    if (this->HasVisibleHandles)
    {
      count += this->Pipeline->Actor->RenderOpaqueGeometry(viewport);
    }
    return count;
  }

  // 3D view: shared rendering — one draw call for ALL representations
  SharedHandleRenderer* shared = GetOrCreateSharedRenderer(this->Renderer);
  if (!shared)
  {
    return count;
  }

  shared->Representations.insert(this);

  if (!shared->GlyphSourcesInitialized)
  {
    shared->Mapper->SetSourceData(GlyphArrow, this->Pipeline->ArrowPolyData);
    shared->Mapper->SetSourceData(GlyphCircle, this->Pipeline->CirclePolyData);
    shared->Mapper->SetSourceData(GlyphRing, this->Pipeline->RingPolyData);
    shared->Mapper->SetSourceData(GlyphCrosshair, this->Pipeline->CrosshairPolyData);
    shared->Mapper->SetSourceData(GlyphArrowOutline, this->Pipeline->ArrowOutlinePolyData);
    shared->Mapper->SetSourceData(GlyphCircleOutline, this->Pipeline->CircleOutlinePolyData);
    shared->Mapper->SetSourceData(GlyphRingOutline, this->Pipeline->RingOutlinePolyData);
    shared->Mapper->SetSourceData(GlyphCrosshairOutline, this->Pipeline->CrosshairOutlinePolyData);
    this->UpdateRelativeCoincidentTopologyOffsets(shared->Mapper);
    shared->GlyphSourcesInitialized = true;
  }

  if (shared->OpaqueRendered)
  {
    return 0;
  }
  shared->OpaqueRendered = true;

  // Pass 1: update all representations and count total instances
  int totalInstances = 0;
  bool anyVisible = false;
  bool anyRepUpdated = false;

  for (auto* rep : shared->Representations)
  {
    if (!rep->Pipeline || !rep->Pipeline->Actor->GetVisibility())
    {
      continue;
    }

    vtkCamera* camera = rep->Renderer ? rep->Renderer->GetActiveCamera() : nullptr;
    vtkMTimeType cameraMTime = camera ? camera->GetMTime() : 0;
    int activeType = rep->GetActiveComponentType();
    int activeIndex = rep->GetActiveComponentIndex();

    bool needsUpdate = (rep->GetMTime() != rep->PostUpdatePipelineMTime)
      || (cameraMTime != rep->LastRenderCameraMTime)
      || (activeType != rep->LastRenderActiveType)
      || (activeIndex != rep->LastRenderActiveIndex);

    if (needsUpdate)
    {
      anyRepUpdated = true;
      rep->UpdateHandleToWorldTransform();
      rep->UpdateSlicePlaneFromSliceNode();
      rep->UpdateCameraState();
      rep->UpdateViewScaleFactor();
      rep->UpdateInteractionPipeline();
      rep->UpdateHandleSize();
      rep->UpdateInstanceArrays();
      rep->UpdateActorTransform();

      rep->PostUpdatePipelineMTime = rep->GetMTime();
      rep->LastRenderCameraMTime = camera ? camera->GetMTime() : 0;
      rep->LastRenderActiveType = activeType;
      rep->LastRenderActiveIndex = activeIndex;
    }

    if (rep->HasVisibleHandles)
    {
      anyVisible = true;
      totalInstances += rep->GetNumberOfHandles() * 2;
    }
  }

  if (!anyVisible)
  {
    // Ensure shared actor has no stale geometry
    if (shared->InstancePolyData->GetNumberOfPoints() > 0)
    {
      shared->InstancePolyData->GetPoints()->SetNumberOfPoints(0);
      shared->InstancePolyData->Modified();
    }
    return 0;
  }

  if (!anyRepUpdated && shared->InstancePolyData->GetNumberOfPoints() == totalInstances)
  {
    shared->Actor->SetPropertyKeys(this->GetPropertyKeys());
    count += shared->Actor->RenderOpaqueGeometry(viewport);
    return count;
  }

  // Pass 2: build shared instance arrays (transform to world space)
  vtkPoints* sharedPoints = shared->InstancePolyData->GetPoints();
  sharedPoints->SetNumberOfPoints(totalInstances);
  shared->GlyphOrientationArray->SetNumberOfTuples(totalInstances);
  shared->GlyphScaleArray->SetNumberOfTuples(totalInstances);
  shared->GlyphSourceIndexArray->SetNumberOfTuples(totalInstances);
  shared->GlyphMaskArray->SetNumberOfTuples(totalInstances);
  shared->ColorArray->SetNumberOfTuples(totalInstances);

  int offset = 0;
  for (auto* rep : shared->Representations)
  {
    if (!rep->Pipeline || !rep->Pipeline->Actor->GetVisibility() || !rep->HasVisibleHandles)
    {
      continue;
    }

    int repInstances = rep->GetNumberOfHandles() * 2;
    vtkMatrix4x4* h2wMatrix = rep->Pipeline->HandleToWorldTransform->GetMatrix();

    double h2w[4][4];
    vtkMatrix4x4::DeepCopy(*h2w, h2wMatrix);

    double h2w_rot[3][3];
    for (int r = 0; r < 3; r++)
    {
      for (int c = 0; c < 3; c++)
      {
        h2w_rot[r][c] = h2w[r][c];
      }
    }
    double q_h2w[4];
    vtkMath::Matrix3x3ToQuaternion(h2w_rot, q_h2w);

    vtkPoints* repPoints = rep->Pipeline->InstancePolyData->GetPoints();

    for (int i = 0; i < repInstances; i++)
    {
      double lp[3];
      repPoints->GetPoint(i, lp);

      double wp[3] = {
        h2w[0][0] * lp[0] + h2w[0][1] * lp[1] + h2w[0][2] * lp[2] + h2w[0][3],
        h2w[1][0] * lp[0] + h2w[1][1] * lp[1] + h2w[1][2] * lp[2] + h2w[1][3],
        h2w[2][0] * lp[0] + h2w[2][1] * lp[1] + h2w[2][2] * lp[2] + h2w[2][3]
      };
      sharedPoints->SetPoint(offset + i, wp);

      float q_local_f[4];
      rep->Pipeline->GlyphOrientationArray->GetTypedTuple(i, q_local_f);
      double q_local[4] = { q_local_f[0], q_local_f[1], q_local_f[2], q_local_f[3] };
      double q_world[4];
      vtkMath::MultiplyQuaternion(q_h2w, q_local, q_world);
      shared->GlyphOrientationArray->SetTuple4(
        offset + i, q_world[0], q_world[1], q_world[2], q_world[3]);

      shared->GlyphScaleArray->SetValue(offset + i, rep->Pipeline->GlyphScaleArray->GetValue(i));
      shared->GlyphMaskArray->SetValue(offset + i, rep->Pipeline->GlyphMaskArray->GetValue(i));
      shared->GlyphSourceIndexArray->SetValue(offset + i, rep->Pipeline->GlyphSourceIndexArray->GetValue(i));

      unsigned char color[4];
      rep->Pipeline->ColorArray->GetTypedTuple(i, color);
      shared->ColorArray->SetTypedTuple(offset + i, color);
    }

    offset += repInstances;
  }

  shared->InstancePolyData->Modified();

  shared->Actor->SetPropertyKeys(this->GetPropertyKeys());
  count += shared->Actor->RenderOpaqueGeometry(viewport);
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
  {
    return count;
  }

  if (!this->GetSliceNode() && this->Renderer)
  {
    // Shared actor is force-opaque — nothing to do in translucent pass.
    return 0;
  }
  else if (this->HasVisibleHandles)
  {
    this->Pipeline->Actor->SetPropertyKeys(this->GetPropertyKeys());
    count += this->Pipeline->Actor->RenderTranslucentPolygonalGeometry(viewport);
  }
  return count;
}

//----------------------------------------------------------------------
vtkTypeBool vtkMRMLInteractionWidgetRepresentation::HasTranslucentPolygonalGeometry()
{
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
  {
    return false;
  }

  if (!this->GetSliceNode() && this->Renderer)
  {
    // Shared actor is force-opaque — always render in the opaque pass
    // so that RenderOpaqueGeometry builds the shared arrays each frame.
    return false;
  }

  if (this->HasVisibleHandles)
  {
    return this->Pipeline->Actor->HasTranslucentPolygonalGeometry();
  }
  return false;
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::InteractionPipeline()
{
  /// Rotation pipeline
  this->RotationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  vtkNew<vtkEllipseArcSource> outerArcSource;
  outerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_OUTER_RADIUS, 0.0, 0.0);
  outerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  outerArcSource->SetCenter(0.0, 0.0, 0.0);
  outerArcSource->SetNormal(0.0, 0.0, 1.0);
  outerArcSource->SetRatio(1.0);
  outerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  outerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  outerArcSource->Update();

  vtkNew<vtkEllipseArcSource> innerArcSource;
  innerArcSource->SetMajorRadiusVector(-INTERACTION_HANDLE_ROTATION_ARC_INNER_RADIUS, 0.0, 0.0);
  innerArcSource->SetResolution(INTERACTION_HANDLE_ROTATION_ARC_RESOLUTION);
  innerArcSource->SetCenter(0.0, 0.0, 0.0);
  innerArcSource->SetNormal(0.0, 0.0, 1.0);
  innerArcSource->SetRatio(1.0);
  innerArcSource->SetStartAngle(180 - INTERACTION_HANDLE_ROTATION_ARC_DEGREES / 2.0);
  innerArcSource->SetSegmentAngle(INTERACTION_HANDLE_ROTATION_ARC_DEGREES);
  innerArcSource->Update();

  vtkNew<vtkPoints> rotationPts;

  this->RingOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->RingOutlinePolyData->SetPoints(rotationPts);
  this->RingOutlinePolyData->SetLines(vtkNew<vtkCellArray>());

  this->RingPolyData = vtkSmartPointer<vtkPolyData>::New();
  this->RingPolyData->SetPoints(rotationPts);
  this->RingPolyData->SetPolys(vtkNew<vtkCellArray>());

  if (INTERACTION_HANDLE_ROTATION_ARC_DEGREES < 360.0)
  {
    vtkNew<vtkIdList> rotationPoly;
    vtkNew<vtkIdList> rotationLine;

    for (int i = 0; i < outerArcSource->GetOutput()->GetNumberOfPoints(); ++i)
    {
      double point[3];
      outerArcSource->GetOutput()->GetPoint(i, point);
      vtkIdType id = rotationPts->InsertNextPoint(point);
      rotationPoly->InsertNextId(id);
      rotationLine->InsertNextId(id);
    }
    for (int i = innerArcSource->GetOutput()->GetNumberOfPoints() - 1; i >= 0; --i)
    {
      double point[3];
      innerArcSource->GetOutput()->GetPoint(i, point);
      vtkIdType id = rotationPts->InsertNextPoint(point);
      rotationPoly->InsertNextId(id);
      rotationLine->InsertNextId(id);
    }
    rotationLine->InsertNextId(0);
    this->ArrowOutlinePolyData->InsertNextCell(VTK_POLY_LINE, rotationLine);
    this->ArrowPolyData->InsertNextCell(VTK_POLYGON, rotationPoly);
  }
  else
  {
    vtkNew<vtkCellArray> rotationTriangles;
    vtkNew<vtkIdList> outerLine;
    vtkNew<vtkIdList> innerLine;

    vtkIdType previousInnerPoint = -1;
    vtkIdType previousOuterPoint = -1;
    for (int index = 0; index < outerArcSource->GetOutput()->GetNumberOfPoints(); ++index)
    {
      double outerLinePoint[3] = { 0.0, 0.0, 0.0 };
      outerArcSource->GetOutput()->GetPoint(index, outerLinePoint);
      vtkIdType outerPointId = rotationPts->InsertNextPoint(outerLinePoint);
      outerLine->InsertNextId(outerPointId);

      double innerLinePoint[3] = { 0.0, 0.0, 0.0 };
      innerArcSource->GetOutput()->GetPoint(index, innerLinePoint);
      vtkIdType innerPointId = rotationPts->InsertNextPoint(innerLinePoint);
      innerLine->InsertNextId(innerPointId);

      if (previousInnerPoint >= 0 && previousOuterPoint >= 0)
      {
        vtkNew<vtkIdList> rotationTriangleA;
        rotationTriangleA->InsertNextId(previousInnerPoint);
        rotationTriangleA->InsertNextId(previousOuterPoint);
        rotationTriangleA->InsertNextId(outerPointId);
        rotationTriangles->InsertNextCell(rotationTriangleA);

        vtkNew<vtkIdList> rotationTriangleB;
        rotationTriangleB->InsertNextId(previousInnerPoint);
        rotationTriangleB->InsertNextId(outerPointId);
        rotationTriangleB->InsertNextId(innerPointId);
        rotationTriangles->InsertNextCell(rotationTriangleB);
      }
      previousInnerPoint = innerPointId;
      previousOuterPoint = outerPointId;
    }

    if (previousInnerPoint > 0 && previousOuterPoint > 0)
    {
      vtkNew<vtkIdList> rotationTriangleA;
      rotationTriangleA->InsertNextId(previousInnerPoint);
      rotationTriangleA->InsertNextId(previousOuterPoint);
      rotationTriangleA->InsertNextId(0);
      rotationTriangles->InsertNextCell(rotationTriangleA);

      vtkNew<vtkIdList> rotationTriangleB;
      rotationTriangleB->InsertNextId(previousInnerPoint);
      rotationTriangleB->InsertNextId(0);
      rotationTriangleB->InsertNextId(1);
      rotationTriangles->InsertNextCell(rotationTriangleB);
    }

    this->RingOutlinePolyData->InsertNextCell(VTK_POLY_LINE, outerLine);
    this->RingOutlinePolyData->InsertNextCell(VTK_POLY_LINE, innerLine);
    this->RingPolyData->SetPolys(rotationTriangles);
  }

  vtkNew<vtkPoints> translationHandlePoints;
  vtkNew<vtkIdList> translationHandlePoly;
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(0.00, 0.00, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_LENGTH, INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_LENGTH, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_SHAFT_RADIUS, 0.00));
  translationHandlePoly->InsertNextId(translationHandlePoints->InsertNextPoint(-INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, -INTERACTION_TRANSLATION_HANDLE_TIP_RADIUS, 0.00));

  vtkNew<vtkIdList> translationHandleLine;
  translationHandleLine->DeepCopy(translationHandlePoly);
  translationHandleLine->InsertNextId(0);

  this->ArrowPolyData = vtkSmartPointer<vtkPolyData>::New();
  this->ArrowPolyData->SetPoints(translationHandlePoints);
  this->ArrowPolyData->SetPolys(vtkNew<vtkCellArray>());
  this->ArrowPolyData->InsertNextCell(VTK_POLYGON, translationHandlePoly);

  this->ArrowOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->ArrowOutlinePolyData->SetPoints(translationHandlePoints);
  this->ArrowOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->ArrowOutlinePolyData->InsertNextCell(VTK_POLY_LINE, translationHandleLine);

  /// Translation pipeline
  this->TranslationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  /// Scale pipeline
  vtkNew<vtkEllipseArcSource> scaleArcSource;
  scaleArcSource->SetMajorRadiusVector(INTERACTION_HANDLE_SCALE_RADIUS, 0.0, 0.0);
  scaleArcSource->SetResolution(100);
  scaleArcSource->SetCenter(0.0, 0.0, 0.0);
  scaleArcSource->SetNormal(0.0, 0.0, 1.0);
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

  this->CirclePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CirclePolyData->SetPoints(scalePoints);
  this->CirclePolyData->SetPolys(vtkNew<vtkCellArray>());
  this->CirclePolyData->InsertNextCell(VTK_POLYGON, scalePoly);

  this->CircleOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CircleOutlinePolyData->SetPoints(scalePoints);
  this->CircleOutlinePolyData->SetLines(vtkNew<vtkCellArray>());
  this->CircleOutlinePolyData->InsertNextCell(VTK_POLY_LINE, scaleLine);

  // Crosshair: four short thin bars pointing away from the center (+X, -X, +Y, -Y), leaving the
  // center empty. Each bar is a thin quad (not a line primitive, see comment above on
  // INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH), with an outline drawn around it, same as the other
  // handle glyphs (circle, arrow, ring).
  vtkNew<vtkPoints> crosshairPoints;
  vtkNew<vtkCellArray> crosshairPolys;
  vtkNew<vtkCellArray> crosshairLines;
  double crosshairBarDirections[4][2] = {
    { 1.0, 0.0 },
    { -1.0, 0.0 },
    { 0.0, 1.0 },
    { 0.0, -1.0 },
  };
  for (int barIndex = 0; barIndex < 4; ++barIndex)
  {
    double along[2] = { crosshairBarDirections[barIndex][0], crosshairBarDirections[barIndex][1] };
    double across[2] = { -along[1], along[0] }; // perpendicular to "along"

    double innerPoint[2] = { along[0] * INTERACTION_HANDLE_CROSSHAIR_INNER_RADIUS, along[1] * INTERACTION_HANDLE_CROSSHAIR_INNER_RADIUS };
    double outerPoint[2] = { along[0] * INTERACTION_HANDLE_CROSSHAIR_OUTER_RADIUS, along[1] * INTERACTION_HANDLE_CROSSHAIR_OUTER_RADIUS };

    double barCorners[4][3] = {
      { innerPoint[0] - across[0] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, innerPoint[1] - across[1] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, 0.0 },
      { outerPoint[0] - across[0] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, outerPoint[1] - across[1] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, 0.0 },
      { outerPoint[0] + across[0] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, outerPoint[1] + across[1] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, 0.0 },
      { innerPoint[0] + across[0] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, innerPoint[1] + across[1] * INTERACTION_HANDLE_CROSSHAIR_BAR_HALF_WIDTH, 0.0 },
    };

    vtkNew<vtkIdList> barPoly;
    vtkNew<vtkIdList> barLine;
    for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
      vtkIdType id = crosshairPoints->InsertNextPoint(barCorners[cornerIndex]);
      barPoly->InsertNextId(id);
      barLine->InsertNextId(id);
    }
    barLine->InsertNextId(barLine->GetId(0));
    crosshairPolys->InsertNextCell(barPoly);
    crosshairLines->InsertNextCell(barLine);
  }

  this->CrosshairPolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CrosshairPolyData->SetPoints(crosshairPoints);
  this->CrosshairPolyData->SetPolys(crosshairPolys);

  this->CrosshairOutlinePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->CrosshairOutlinePolyData->SetPoints(crosshairPoints);
  this->CrosshairOutlinePolyData->SetLines(crosshairLines);

  this->ScaleHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->HandleToWorldTransform = vtkSmartPointer<vtkTransform>::New();

  this->InstancePolyData = vtkSmartPointer<vtkPolyData>::New();
  this->InstancePolyData->SetPoints(vtkSmartPointer<vtkPoints>::New());

  this->GlyphOrientationArray = vtkSmartPointer<vtkFloatArray>::New();
  this->GlyphOrientationArray->SetName("orientation");
  this->GlyphOrientationArray->SetNumberOfComponents(4);
  this->InstancePolyData->GetPointData()->AddArray(this->GlyphOrientationArray);

  this->GlyphScaleArray = vtkSmartPointer<vtkFloatArray>::New();
  this->GlyphScaleArray->SetName("scale");
  this->GlyphScaleArray->SetNumberOfComponents(1);
  this->InstancePolyData->GetPointData()->AddArray(this->GlyphScaleArray);

  this->GlyphSourceIndexArray = vtkSmartPointer<vtkIntArray>::New();
  this->GlyphSourceIndexArray->SetName("glyphType");
  this->GlyphSourceIndexArray->SetNumberOfComponents(1);
  this->InstancePolyData->GetPointData()->AddArray(this->GlyphSourceIndexArray);

  this->GlyphMaskArray = vtkSmartPointer<vtkUnsignedCharArray>::New();
  this->GlyphMaskArray->SetName("mask");
  this->GlyphMaskArray->SetNumberOfComponents(1);
  this->InstancePolyData->GetPointData()->AddArray(this->GlyphMaskArray);

  this->ColorArray = vtkSmartPointer<vtkUnsignedCharArray>::New();
  this->ColorArray->SetName("color");
  this->ColorArray->SetNumberOfComponents(4);
  this->InstancePolyData->GetPointData()->AddArray(this->ColorArray);

  this->Mapper = vtkSmartPointer<vtkGlyph3DMapper>::New();
  this->Mapper->SetInputData(this->InstancePolyData);
  this->Mapper->SetSourceData(GlyphArrow, this->ArrowPolyData);
  this->Mapper->SetSourceData(GlyphCircle, this->CirclePolyData);
  this->Mapper->SetSourceData(GlyphRing, this->RingPolyData);
  this->Mapper->SetSourceData(GlyphCrosshair, this->CrosshairPolyData);
  this->Mapper->SetSourceData(GlyphArrowOutline, this->ArrowOutlinePolyData);
  this->Mapper->SetSourceData(GlyphCircleOutline, this->CircleOutlinePolyData);
  this->Mapper->SetSourceData(GlyphRingOutline, this->RingOutlinePolyData);
  this->Mapper->SetSourceData(GlyphCrosshairOutline, this->CrosshairOutlinePolyData);
  this->Mapper->SetSourceIndexing(true);
  this->Mapper->SetSourceIndexArray("glyphType");
  this->Mapper->OrientOn();
  this->Mapper->SetOrientationModeToQuaternion();
  this->Mapper->SetOrientationArray("orientation");
  this->Mapper->ScalingOn();
  this->Mapper->SetScaleModeToScaleByMagnitude();
  this->Mapper->SetScaleArray("scale");
  this->Mapper->SetMasking(true);
  this->Mapper->SetMaskArray("mask");
  this->Mapper->SetColorModeToDirectScalars();
  this->Mapper->ScalarVisibilityOn();
  this->Mapper->SetScalarModeToUsePointFieldData();
  this->Mapper->SelectColorArray("color");

  this->Property = vtkSmartPointer<vtkProperty>::New();
  this->Property->SetPointSize(1.e-6);
  this->Property->SetLineWidth(1.0);
  this->Property->SetDiffuse(0.0);
  this->Property->SetAmbient(1.0);
  this->Property->SetMetallic(0.0);
  this->Property->SetSpecular(0.0);
  this->Property->SetEdgeVisibility(false);
  this->Property->SetOpacity(1.0);

  this->Actor = vtkSmartPointer<vtkActor>::New();
  this->Actor->SetProperty(this->Property);
  this->Actor->SetMapper(this->Mapper);

  this->WorldToSliceTransform = vtkSmartPointer<vtkTransform>::New();
  this->ActorTransform = vtkSmartPointer<vtkTransform>::New();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation::InteractionPipeline::~InteractionPipeline() = default;

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::InitializePipeline()
{
  if (vtkMapper::GetResolveCoincidentTopology() != VTK_RESOLVE_POLYGON_OFFSET)
  {
    vtkGenericWarningMacro("Unexpected resolve coincident topology value: " << vtkMapper::GetResolveCoincidentTopology());
  }
  this->UpdateRelativeCoincidentTopologyOffsets(this->Pipeline->Mapper);

  this->CreateRotationHandles();
  this->CreateTranslationHandles();
  this->CreateScaleHandles();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateRotationHandles()
{
  vtkNew<vtkPoints> points;

  double xRotationHandle[3] = { 0.0, 0.0, 0.0 }; // X-axis
  points->InsertNextPoint(xRotationHandle);
  double yRotationHandle[3] = { 0.0, 0.0, 0.0 }; // Y-axis
  vtkMath::Normalize(yRotationHandle);
  points->InsertNextPoint(yRotationHandle);
  double zRotationHandle[3] = { 0.0, 0.0, 0.0 }; // Z-axis
  vtkMath::Normalize(zRotationHandle);
  points->InsertNextPoint(zRotationHandle);
  this->Pipeline->RotationHandlePoints->SetPoints(points);
  double viewPlaneRotationHandle[3] = { 0.0, 0.0, 0.0 }; // View
  this->Pipeline->RotationHandlePoints->GetPoints()->InsertNextPoint(viewPlaneRotationHandle);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  vtkNew<vtkTransform> xRotationOrientation;
  xRotationOrientation->RotateX(90);
  xRotationOrientation->RotateY(90);
  xRotationOrientation->RotateZ(45);
  vtkMatrix4x4* xRotationMatrix = xRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(xRotationMatrix->GetElement(0, 0),
                                     xRotationMatrix->GetElement(1, 0),
                                     xRotationMatrix->GetElement(2, 0),
                                     xRotationMatrix->GetElement(0, 1),
                                     xRotationMatrix->GetElement(1, 1),
                                     xRotationMatrix->GetElement(2, 1),
                                     xRotationMatrix->GetElement(0, 2),
                                     xRotationMatrix->GetElement(1, 2),
                                     xRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> yRotationOrientation;
  yRotationOrientation->RotateX(90);
  yRotationOrientation->RotateZ(45);
  vtkMatrix4x4* yRotationMatrix = yRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(yRotationMatrix->GetElement(0, 0),
                                     yRotationMatrix->GetElement(1, 0),
                                     yRotationMatrix->GetElement(2, 0),
                                     yRotationMatrix->GetElement(0, 1),
                                     yRotationMatrix->GetElement(1, 1),
                                     yRotationMatrix->GetElement(2, 1),
                                     yRotationMatrix->GetElement(0, 2),
                                     yRotationMatrix->GetElement(1, 2),
                                     yRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> zRotationOrientation;
  zRotationOrientation->RotateZ(45);
  vtkMatrix4x4* zRotationMatrix = zRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(zRotationMatrix->GetElement(0, 0),
                                     zRotationMatrix->GetElement(1, 0),
                                     zRotationMatrix->GetElement(2, 0),
                                     zRotationMatrix->GetElement(0, 1),
                                     zRotationMatrix->GetElement(1, 1),
                                     zRotationMatrix->GetElement(2, 1),
                                     zRotationMatrix->GetElement(0, 2),
                                     zRotationMatrix->GetElement(1, 2),
                                     zRotationMatrix->GetElement(2, 2));

  orientationArray->InsertNextTuple9(1, 0, 0, 0, 1, 0, 0, 0, 1); // View
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
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0); // Free translation
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(orientationArray);

  vtkNew<vtkIdTypeArray> visibilityArray;
  visibilityArray->SetName("visibility");
  visibilityArray->SetNumberOfComponents(1);
  visibilityArray->SetNumberOfValues(this->Pipeline->TranslationHandlePoints->GetNumberOfPoints());
  visibilityArray->Fill(1);
  this->Pipeline->TranslationHandlePoints->GetPointData()->AddArray(visibilityArray);
}

//----------------------------------------------------------------------
vtkProp* vtkMRMLInteractionWidgetRepresentation::GetInteractionActor()
{
  if (!this->Pipeline)
  {
    return nullptr;
  }
  return this->Pipeline->Actor;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateCameraState()
{
  if (this->GetSliceNode())
  {
    this->CachedParallelProjection = true;
    double normal4[4] = { 0.0, 0.0, 1.0, 0.0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(normal4, normal4);
    this->CachedCameraDirection[0] = normal4[0];
    this->CachedCameraDirection[1] = normal4[1];
    this->CachedCameraDirection[2] = normal4[2];
    vtkMath::Normalize(this->CachedCameraDirection);

    double viewUp4[4] = { 0.0, 1.0, 0.0, 0.0 };
    double viewUp4Out[4] = { 0.0, 1.0, 0.0, 0.0 };
    this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(viewUp4, viewUp4Out);
    this->CachedViewUp_World[0] = viewUp4Out[0];
    this->CachedViewUp_World[1] = viewUp4Out[1];
    this->CachedViewUp_World[2] = viewUp4Out[2];
  }
  else if (this->GetRenderer() && this->GetRenderer()->GetActiveCamera())
  {
    vtkCamera* camera = this->GetRenderer()->GetActiveCamera();
    if (camera->GetParallelProjection())
    {
      this->CachedParallelProjection = true;
      camera->GetViewPlaneNormal(this->CachedCameraDirection);
    }
    else
    {
      this->CachedParallelProjection = false;
      camera->GetPosition(this->CachedCameraPosition);
    }
    camera->GetViewUp(this->CachedViewUp_World);
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::CreateScaleHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(INTERACTION_HANDLE_SCALE_DISTANCE_FROM_CENTER, 0.0, 0.0); // X-axis
  points->InsertNextPoint(0.0, INTERACTION_HANDLE_SCALE_DISTANCE_FROM_CENTER, 0.0); // Y-axis
  points->InsertNextPoint(0.0, 0.0, INTERACTION_HANDLE_SCALE_DISTANCE_FROM_CENTER); // Z-axis
  this->Pipeline->ScaleHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
  orientationArray->InsertNextTuple9(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
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
  for (int type = InteractionNone + 1; type < Interaction_Last; ++type)
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
    vtkErrorMacro("GetNumberOfHandles: Invalid handle type: " << type);
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
void vtkMRMLInteractionWidgetRepresentation::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
  {
    return;
  }

  double red[3] = { 0.80, 0.35, 0.35 };
  double redSelected[3] = { 0.70, 0.07, 0.07 };

  double green[3] = { 0.35, 0.70, 0.35 };
  double greenSelected[3] = { 0.00, 0.50, 0.00 };

  double blue[3] = { 0.35, 0.35, 0.8 };
  double blueSelected[3] = { 0.07, 0.07, 0.70 };

  double white[3] = { 0.80, 0.80, 0.80 };
  double whiteSelected[3] = { 1.00, 1.00, 1.00 };

  bool selected = this->GetActiveComponentType() == type && this->GetActiveComponentIndex() == index;

  double* currentColor = white;
  switch (index)
  {
    case 0: currentColor = selected ? redSelected : red; break;
    case 1: currentColor = selected ? greenSelected : green; break;
    case 2: currentColor = selected ? blueSelected : blue; break;
    case 3: currentColor = selected ? whiteSelected : white; break;
    default: currentColor = selected ? whiteSelected : white; break;
  }

  double opacity = this->GetHandleOpacity(type, index);
  if (selected)
  {
    opacity = this->GetInteractionHandleOpacity();
  }

  for (int i = 0; i < 3; ++i)
  {
    color[i] = currentColor[i];
  }
  color[3] = opacity;
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation::GetHandleGlyphType(int type, int index)
{
  if (type == InteractionRotationHandle)
  {
    return GlyphRing;
  }
  else if (type == InteractionTranslationHandle && index < 3)
  {
    return GlyphArrow;
  }
  return GlyphCircle;
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
  if (this->CachedHandleOpacity >= 0.0)
  {
    double result = this->CachedHandleOpacity;
    this->CachedHandleOpacity = -1.0;
    return result;
  }

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
    return opacity * this->GetInteractionHandleOpacity();
  }

  double handlePosition_World[3] = { 0.0, 0.0, 0.0 };
  this->GetInteractionHandlePositionWorld(type, index, handlePosition_World);

  double viewNormal_World[3] = { 0.0, 0.0, 0.0 };
  this->GetHandleToCameraVectorWorld(handlePosition_World, viewNormal_World);
  if (vtkMath::Dot(viewNormal_World, axis_World) < 0.0)
  {
    vtkMath::MultiplyScalar(axis_World, -1.0);
  }

  double fadeAngleRange = this->StartFadeAngleDegrees - this->EndFadeAngleDegrees;
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal_World, axis_World));
  if (type == InteractionRotationHandle)
  {
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
  return opacity * this->GetInteractionHandleOpacity();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetHandleToCameraVectorWorld(double handlePosition_World[3], double normal_World[3])
{
  if (!handlePosition_World || !normal_World)
  {
    vtkErrorMacro("GetHandleToCameraVectorWorld: Invalid arguments");
    return;
  }

  if (this->CachedParallelProjection)
  {
    normal_World[0] = this->CachedCameraDirection[0];
    normal_World[1] = this->CachedCameraDirection[1];
    normal_World[2] = this->CachedCameraDirection[2];
  }
  else
  {
    vtkMath::Subtract(this->CachedCameraPosition, handlePosition_World, normal_World);
    vtkMath::Normalize(normal_World);
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::SetWidgetScale(double scale)
{
  if (this->WidgetScale == scale)
  {
    return;
  }
  this->WidgetScale = scale;
  this->NeedToRenderOn();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleOriginWorld(double originWorld[3])
{
  if (!originWorld)
  {
    return;
  }

  double handleOrigin[3] = { 0.0, 0.0, 0.0 };
  this->Pipeline->HandleToWorldTransform->TransformPoint(handleOrigin, originWorld);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleAxisLocal(int type, int index, double axisLocal[3])
{
  if (!axisLocal)
  {
    vtkErrorWithObjectMacro(nullptr, "GetInteractionHandleAxisLocal: Invalid axis argument");
    return;
  }

  axisLocal[0] = 0.0;
  axisLocal[1] = 0.0;
  axisLocal[2] = 0.0;

  bool faceCamera = false;

  switch (index)
  {
    case 0: axisLocal[0] = 1.0; break;
    case 1: axisLocal[1] = 1.0; break;
    case 2: axisLocal[2] = 1.0; break;
    default: faceCamera = type == InteractionRotationHandle; break;
  }

  if (faceCamera)
  {
    double handlePosition_World[3] = { 0.0, 0.0, 0.0 };
    this->GetInteractionHandlePositionWorld(type, index, handlePosition_World);

    double axisWorld[3] = { 0.0, 0.0, 0.0 };
    this->GetHandleToCameraVectorWorld(handlePosition_World, axisWorld);

    vtkNew<vtkTransform> worldToHandleTransform;
    worldToHandleTransform->DeepCopy(this->GetHandleToWorldTransform());
    worldToHandleTransform->Inverse();
    worldToHandleTransform->TransformVector(axisWorld, axisLocal);
    vtkMath::Normalize(axisLocal);
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

  vtkNew<vtkTransform> handleScaleTransform;
  handleScaleTransform->Scale(this->WidgetScale, this->WidgetScale, this->WidgetScale);
  handleScaleTransform->TransformPoint(positionWorld, positionWorld);

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

  int glyphType = this->GetHandleGlyphType(type, index);

  bool applyScaleToPosition = this->GetApplyScaleToPosition(type, index);

  return HandleInfo(index, type, handlePositionWorld, handlePositionLocal, color, glyphType, applyScaleToPosition);
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetApplyScaleToPosition(int vtkNotUsed(type), int vtkNotUsed(index))
{
  return true;
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
void vtkMRMLInteractionWidgetRepresentation::GetSliceToWorldCoordinates(const double slicePos[2], double worldPos[3])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!this->Renderer || !sliceNode)
  {
    return;
  }

  double xyzw[4] = { slicePos[0] - this->Renderer->GetOrigin()[0], slicePos[1] - this->Renderer->GetOrigin()[1], 0.0, 1.0 };
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

  if (!this->Renderer)
  {
    return;
  }

  vtkMatrix4x4* sliceXYToRAS = this->GetSliceNode()->GetXYToRAS();
  if (this->Pipeline)
  {
    // Update transformation to slice
    vtkNew<vtkMatrix4x4> rasToXY;
    rasToXY->DeepCopy(sliceXYToRAS);
    rasToXY->Invert();
    this->Pipeline->WorldToSliceTransform->Identity();
    this->Pipeline->WorldToSliceTransform->PostMultiply();
    this->Pipeline->WorldToSliceTransform->Concatenate(rasToXY);

    // TODO: Use 'vtkCamera::GetCompositeProjectionTransformMatrix()'
    // int* size = this->Renderer->GetSize();
    // double aspect = double(size[1]) / double(size[0]);
    // vtkNew<vtkMatrix4x4> compositeProjectionTransformMatrix;
    // compositeProjectionTransformMatrix->DeepCopy(this->Renderer->GetActiveCamera()->GetCompositeProjectionTransformMatrix(aspect, -1, +1));
    // compositeProjectionTransformMatrix->Invert();
    // this->Pipeline->WorldToSliceTransform->Concatenate(compositeProjectionTransformMatrix);

    int* dimensions = this->GetSliceNode()->GetDimensions();
    if (this->Renderer->GetActiveCamera()->GetUseHorizontalViewAngle())
    {
      this->Pipeline->WorldToSliceTransform->Scale(2.0 / dimensions[0], 2.0 / dimensions[0], 2.0 / dimensions[0]);
      this->Pipeline->WorldToSliceTransform->Translate(-1.0, -1.0 * dimensions[1] / dimensions[0], 0.0);
    }
    else
    {
      this->Pipeline->WorldToSliceTransform->Scale(2.0 / dimensions[1], 2.0 / dimensions[1], 2.0 / dimensions[1]);
      this->Pipeline->WorldToSliceTransform->Translate(-1.0 * dimensions[0] / dimensions[1], -1.0, 0.0);
    }

    // Move the interaction handle to the slice plane to prevent it from being clipped.
    double handleCenterPos_Slice[3] = { 0.0, 0.0, 0.0 };
    this->Pipeline->HandleToWorldTransform->TransformPoint(handleCenterPos_Slice, handleCenterPos_Slice);
    this->Pipeline->WorldToSliceTransform->TransformPoint(handleCenterPos_Slice, handleCenterPos_Slice);
    this->Pipeline->WorldToSliceTransform->Translate(0.0, 0.0, -handleCenterPos_Slice[2]);
  }

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
    this->ViewScaleFactorMmPerPixel = sqrt(xyToSlice->GetElement(0, 1) * xyToSlice->GetElement(0, 1) + xyToSlice->GetElement(1, 1) * xyToSlice->GetElement(1, 1));
  }
  else
  {
    double handlePoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetHandleToWorldTransform()->TransformPoint(handlePoint_World, handlePoint_World);

    double cameraPos_World[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetActiveCamera()->GetPosition(cameraPos_World);

    double cameraDirection_World[3] = { 0.0, 0.0, 0.0 };
    this->Renderer->GetActiveCamera()->GetDirectionOfProjection(cameraDirection_World);
    vtkMath::Normalize(cameraDirection_World);

    // Use projected depth (along view direction) instead of Euclidean distance
    // so that lateral position on screen doesn't affect handle size.
    double handleToCamera[3];
    vtkMath::Subtract(handlePoint_World, cameraPos_World, handleToCamera);
    double depth = vtkMath::Dot(handleToCamera, cameraDirection_World);
    vtkMath::MultiplyScalar(cameraDirection_World, depth);

    double handleFocalPoint_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(cameraPos_World, cameraDirection_World, handleFocalPoint_World);

    // What we are interested in is the Mm to pixel conversion for an object that is as far away from the camera as the handle.
    // In VR we can't use the scale factor at the handle position since the scale will change when the user rotates their head.
    // The solution is to find the mm to pixel conversion for a point that is as far away from the camera as the handle, but in
    // the camera view direction.
    this->ViewScaleFactorMmPerPixel = vtkMRMLAbstractThreeDViewDisplayableManager::GetViewScaleFactorAtPosition(this->Renderer, handleFocalPoint_World);
  }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation::UpdateHandleSize()
{
  if (!this->GetInteractionSizeAbsolute())
  {
    this->InteractionSize = this->ScreenSizePixel * this->GetScreenScaleFactor() * this->GetInteractionScalePercent() / 100.0 * this->ViewScaleFactorMmPerPixel;
  }
  else
  {
    this->InteractionSize = this->GetInteractionSizeMm();
    if (this->GetSliceNode())
    {
      // Convert to pixels for slice views
      this->InteractionSize /= this->ViewScaleFactorMmPerPixel;
    }
  }
  this->SetWidgetScale(this->InteractionSize);
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionScalePercent()
{
  return 3.0;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionSizeMm()
{
  return this->InteractionSize;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation::GetInteractionSizeAbsolute()
{
  return false;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetInteractionHandleOpacity()
{
  return 1.0;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation::GetTranslationScaleFactor()
{
  return 1.0;
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
